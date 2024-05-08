// Build an MLT XML file which kdenlive can load
// Author: Max Schwarz <max.schwarz@online.de>

#include <mlt++/Mlt.h>
#include <poppler-qt6.h>

#include <QImage>
#include <QUrl>

#include <locale.h>

#include <filesystem>

constexpr int WIDTH = 1920;
constexpr int HEIGHT = 1080;
constexpr int FPS = 25;
constexpr int DEFAULT_SLIDE_LENGTH = 100;

namespace fs = std::filesystem;

struct Track
{
    explicit Track(Mlt::Profile& profile)
     : playlist{profile}
     , tractor{profile}
    {
        playlist.set("hide", 2);

        tractor.set_track(playlist, 0);
        tractor.set("hide", 2);
    }

    Mlt::Playlist playlist;

    Mlt::Tractor tractor;

    int time = 0;
};

struct EmbeddedVideo
{
    std::unique_ptr<Mlt::Chain> producer;
    mlt_rect rect;
    int length;
};

int main(int argc, char** argv)
{
    if(argc != 3)
    {
        fprintf(stderr, "Usage: beamer2mlt <in.pdf> <out.mlt>\n");
        return 1;
    }

    const char* inputFile = argv[1];
    const char* outputFile = argv[2];

    auto document = Poppler::Document::load(inputFile);
    if(!document || document->isLocked())
    {
        fprintf(stderr, "Could not load input file %s", inputFile);
        return 1;
    }

    Mlt::Factory::init();
    Mlt::Profile profile("HD 1080p 25 fps");
    profile.set_width(WIDTH);
    profile.set_height(HEIGHT);
    profile.set_progressive(1);
    profile.set_sample_aspect(1, 1);
    profile.set_display_aspect(16, 9);
    profile.set_colorspace(709);


    std::vector<std::unique_ptr<Track>> tracks;

    // Slide track
    tracks.push_back(std::make_unique<Track>(profile));

    QUrl documentUrl{inputFile};
    fs::path outputPath = fs::absolute(fs::path{outputFile}).parent_path();

    fs::path slidesDir = outputPath / "slides";
    if(!fs::exists(slidesDir))
    {
        std::error_code ec;
        if(!fs::create_directory(slidesDir, ec))
        {
            fprintf(stderr, "Could not create directory '%s': %s\n", slidesDir.c_str(), ec.message().c_str());
            return 1;
        }
    }

    int totalTime = 0;

    for(int pageNumber = 0; pageNumber < document->numPages(); ++pageNumber)
    {
        auto page = document->page(pageNumber);
        if(!page)
        {
            fprintf(stderr, "Could not load page %d\n", pageNumber);
            return 1;
        }

        QSizeF pageSize = page->pageSizeF() / 72.0;
        float dpi = std::min(
            WIDTH / pageSize.width(),
            HEIGHT / pageSize.height()
        );
        QImage image = page->renderToImage(dpi, dpi);

        fs::path framePath = fs::absolute(
            slidesDir / QString("slide%1.png").arg(pageNumber, 4, 10, QChar('0')).toStdString()
        );

        image.save(QString::fromStdString(framePath.string()));

        Mlt::Producer frameProd(profile, "qimage", framePath.c_str());
        frameProd.set("kdenlive:clip_type", 2);

        int frameLength = (page->duration() > 0) ? page->duration() : DEFAULT_SLIDE_LENGTH;

        frameProd.set("hide", 2);
        if(!frameProd.is_valid())
        {
            fprintf(stderr, "Could not load frame %s into MLT\n", framePath.c_str());
            return 1;
        }

        std::vector<EmbeddedVideo> videos;
        int minLength = std::numeric_limits<int>::max();
        int maxLength = 0;
        for(auto& link : page->links())
        {
            if(link->linkType() != Poppler::Link::Execute)
                continue;

            EmbeddedVideo video;

            auto execLink = reinterpret_cast<Poppler::LinkExecute*>(link.get());

            QUrl url = execLink->fileName();
            if(url.isRelative())
                url = documentUrl.resolved(url);

            url = url.adjusted(QUrl::RemoveQuery);

            fs::path videoPath = fs::absolute(url.toString().toStdString());

            video.producer = std::make_unique<Mlt::Chain>(profile, videoPath.c_str());
            if(!video.producer->is_valid())
            {
                fprintf(stderr, "Could not load video %s into MLT\n", videoPath.c_str());
                return 1;
            }

            video.length = QString(video.producer->get_length_time(mlt_time_frames)).toInt();
            minLength = std::min(minLength, video.length);
            maxLength = std::max(maxLength, video.length);

            video.producer->set("kdenlive:clip_type", 2);
            video.producer->set("hide", 2);
            video.producer->set("set.test_audio", 1); // Mutes audio (strange naming)
            video.producer->set("set.test_image", 0);
            // video.producer->set("eof", "loop");

            auto area = execLink->linkArea().normalized();
            mlt_rect rect;
            video.rect.x = (area.left() * WIDTH);
            video.rect.y = (area.top() * HEIGHT);
            video.rect.w = (area.width() * WIDTH);
            video.rect.h = (area.height() * HEIGHT);
            video.rect.o = 1.0;

            videos.push_back(std::move(video));
        }

        if(!videos.empty())
        {
            // Adjust frame duration to match video
            if(page->duration() < 0)
            {
                frameLength = maxLength;
            }

            int trackIdx = 1;
            for(auto& video : videos)
            {
                if(trackIdx >= tracks.size())
                    tracks.push_back(std::make_unique<Track>(profile));

                auto& track = tracks[trackIdx];

                if(track->time < totalTime)
                {
                    // Insert blank to get to start of slide
                    Mlt::Producer blank(profile, "blank");
                    int duration = totalTime - track->time;
                    track->playlist.blank(qPrintable(QString::number(duration)));
                    track->time += duration;
                }

                // Loop the video until end of slide
                while(track->time < totalTime + frameLength)
                {
                    const int remaining = totalTime + frameLength - track->time;
                    const int clipLen = std::min(remaining, video.length);

                    track->playlist.append(*video.producer, 0, clipLen);

                    // Attach filter to this
                    Mlt::Filter filter(profile, "qtblend");
                    filter.set("kdenlive_id", "qtblend");
                    filter.anim_set("rect", video.rect, 0);

                    track->playlist.get_clip(track->playlist.count()-1)->attach(filter);

                    track->time += clipLen;
                }

                trackIdx++;
            }
        }

        frameProd.set("length", qPrintable(QString::number(frameLength)));

        tracks[0]->playlist.append(frameProd);

        totalTime += frameLength;
    }

    Mlt::Tractor tractor(profile);
    tractor.set("hide", 2);
    for(int i = 0; i < tracks.size(); ++i)
    {
        tractor.set_track(tracks[i]->playlist, i);

        if(i != 0)
        {
            Mlt::Transition blend(profile, "qtblend");
            blend.set("internal_added", 327);
            tractor.plant_transition(blend, 0, i);
        }
    }

    setlocale(LC_NUMERIC, "C");

    Mlt::Consumer consumer(profile, "xml", outputFile);
    consumer.connect(tractor);
    consumer.debug();
    consumer.run();

    return 0;
}
