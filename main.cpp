// Build an MLT XML file which kdenlive can load
// Author: Max Schwarz <max.schwarz@online.de>

#include <mlt++/Mlt.h>
#include <poppler-qt6.h>

#include <QImage>
#include <QUrl>

#include <locale.h>

constexpr int WIDTH = 1920;
constexpr int HEIGHT = 1080;
constexpr int FPS = 25;
constexpr int DEFAULT_SLIDE_LENGTH = 100;

struct Track
{
    explicit Track(Mlt::Profile& profile)
     : playlist{profile}
     , tractor{profile}
    {
        playlist.set("hide", 2);

        tractor.set_track(playlist, 0);
    }

    Mlt::Playlist playlist;

    Mlt::Tractor tractor;

    int time = 0;
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
    Mlt::Profile profile;
    profile.set_width(WIDTH);
    profile.set_height(HEIGHT);
    profile.set_progressive(1);


    std::vector<std::unique_ptr<Track>> tracks;

    for(int i = 0; i < 8; ++i)
        tracks.push_back(std::make_unique<Track>(profile));


    QUrl documentUrl{argv[1]};

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

        auto frameName = QString("frame%1.png").arg(pageNumber, 4, 10, QChar('0'));
        image.save(frameName);

        Mlt::Producer frameProd(profile, "qimage", qPrintable(frameName));

        int frameLength = DEFAULT_SLIDE_LENGTH;

        frameProd.set("hide", 2);
        if(!frameProd.is_valid())
        {
            fprintf(stderr, "Could not load frame %s into MLT\n", qPrintable(frameName));
            return 1;
        }

        int trackIdx = 1;
        for(auto& link : page->links())
        {
            if(link->linkType() != Poppler::Link::Execute)
                continue;

            if(trackIdx >= tracks.size())
                break;

            auto execLink = reinterpret_cast<Poppler::LinkExecute*>(link.get());

            QUrl url = execLink->fileName();
            if(url.isRelative())
                url = documentUrl.resolved(url);

            url = url.adjusted(QUrl::RemoveQuery);

            Mlt::Producer producer(profile, qPrintable(url.toString()));
            if(!producer.is_valid())
            {
                fprintf(stderr, "Could not load video %s into MLT\n", qPrintable(url.toString()));
                return 1;
            }

            int my_length = QString(producer.get_length_time(mlt_time_frames)).toInt();
            frameLength = std::max(my_length, frameLength);

            producer.set("hide", 2);

            Mlt::Filter filter(profile, "qtblend");
            filter.set("kdenlive_id", "qtblend");

            auto area = execLink->linkArea().normalized();
            mlt_rect rect;
            rect.x = (area.left() * WIDTH);
            rect.y = (area.top() * HEIGHT);
            rect.w = (area.width() * WIDTH);
            rect.h = (area.height() * HEIGHT);
            rect.o = 1.0;

            filter.anim_set("rect", rect, 0);

            auto& track = tracks[trackIdx];
            if(track->time < totalTime)
            {
                // Insert blank
                Mlt::Producer blank(profile, "blank");
                int duration = totalTime - track->time;
                track->playlist.blank(qPrintable(QString::number(duration)));
                track->time += duration;
            }

            track->playlist.append(producer);

            // Attach filter to this
            track->playlist.get_clip(track->playlist.count()-1)->attach(filter);

            track->time += my_length;

            trackIdx++;
        }

        frameProd.set("length", qPrintable(QString::number(frameLength)));

        tracks[0]->playlist.append(frameProd);

        totalTime += frameLength;
    }

    Mlt::Tractor tractor(profile);
    for(int i = 0; i < tracks.size(); ++i)
    {
        tractor.set_track(tracks[i]->tractor, i);

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
