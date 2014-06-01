#include "mpris.hpp"
#include "playengine.hpp"
#include "playlistmodel.hpp"
#include "mainwindow.hpp"

namespace mpris {

auto getMainWindow() -> MainWindow*
{
    auto tops = qApp->topLevelWidgets();
    for (auto w : tops) {
        if (auto mw = qobject_cast<MainWindow*>(w))
            return mw;
    }
    return nullptr;
}

RootObject::RootObject(QObject *parent)
    : QObject(parent)
{
    m_name = u"org.mpris.MediaPlayer2.CMPlayer"_q;
    auto bus = QDBusConnection::sessionBus();
    auto ok = bus.registerService(m_name);
    if (!ok) {
        m_name = m_name % ".instance" % QString::number(qApp->applicationPid());
        ok = bus.registerService(m_name);
    }
    if (!ok)
        m_name.clear();
    else {
        m_mp2 = new MediaPlayer2(this);
        m_player = new Player(this);
        bus.registerObject("/org/mpris/MediaPlayer2", this,
                           QDBusConnection::ExportAdaptors);
    }
}

RootObject::~RootObject()
{
    if (m_name.isEmpty())
        return;
    QDBusConnection::sessionBus().unregisterObject("/org/mpris/MediaPlayer2");
    QDBusConnection::sessionBus().unregisterService(m_name);
}


static auto dbusTrackId(const Mrl &mrl) -> QString
{
    using Hash = QCryptographicHash;
    const auto hash = Hash::hash(mrl.toString().toLocal8Bit(), Hash::Md5);
    return u"/net/xylosper/CMPlayer/track_"_q + hash.toHex();
}

static auto sendPropertiesChanged(const QDBusAbstractAdaptor *adaptor,
                                  const QVariantMap &properties) -> void
{
    const auto iface = _L(adaptor->metaObject()->classInfo(0).value());
    auto sig = QDBusMessage::createSignal("/org/mpris/MediaPlayer2",
                                          "org.freedesktop.DBus.Properties",
                                          "PropertiesChanged" );
    sig << iface << properties << QStringList();
    QDBusConnection::sessionBus().send(sig);
}

SIA sendPropertiesChanged(const QDBusAbstractAdaptor *adaptor,
                          const char *property, const QVariant &value) -> void
{
    QVariantMap props;
    props.insert(property, value); sendPropertiesChanged(adaptor, props);
}

struct MediaPlayer2::Data {
    MainWindow *mw = nullptr;
};

MediaPlayer2::MediaPlayer2(QObject *parent)
: QDBusAbstractAdaptor(parent), d(new Data) {
    d->mw = getMainWindow();
    Q_ASSERT(d->mw);
    connect(d->mw, &MainWindow::fullscreenChanged, [this] (bool fs) {
        sendPropertiesChanged(this, "Fullscreen", fs);
    });
}

MediaPlayer2::~MediaPlayer2() {
    delete d;
}

auto MediaPlayer2::isFullScreen() const -> bool
{
    return d->mw->isFullScreen();
}

auto MediaPlayer2::setFullScreen(bool fs) -> void
{
    d->mw->setFullScreen(fs);
}

auto MediaPlayer2::Raise() -> void
{
    d->mw->wake();
}

auto MediaPlayer2::Quit() -> void
{
    d->mw->exit();
}

auto MediaPlayer2::supportedUriSchemes() const -> QStringList
{
    return QStringList() << "file" << "http" << "https" << "ftp"
                         << "mms" << "rtmp" << "rtsp" << "sftp";
}

auto MediaPlayer2::supportedMimeTypes() const -> QStringList
{
    return QStringList() << "application/ogg" << "application/x-ogg"
                         << "application/sdp" << "application/smil"
                         << "application/x-smil" << "application/streamingmedia"
                         << "application/x-streamingmedia"
                         << "application/vnd.rn-realmedia"
                         << "application/vnd.rn-realmedia-vbr"
                         << "audio/aac" << "audio/x-aac" << "audio/m4a"
                         << "audio/x-m4a" << "audio/mp1" << "audio/x-mp1"
                         << "audio/mp2" << "audio/x-mp2" << "audio/mp3"
                         << "audio/x-mp3" << "audio/mpeg" << "audio/x-mpeg"
                         << "audio/mpegurl" << "audio/x-mpegurl" << "audio/mpg"
                         << "audio/x-mpg" << "audio/rn-mpeg" << "audio/scpls"
                         << "audio/x-scpls" << "audio/vnd.rn-realaudio"
                         << "audio/wav" << "audio/x-pn-windows-pcm"
                         << "audio/x-realaudio" << "audio/x-pn-realaudio"
                         << "audio/x-ms-wma" << "audio/x-pls" << "audio/x-wav"
                         << "video/mpeg" << "video/x-mpeg" << "video/x-mpeg2"
                         << "video/mp4" << "video/msvideo" << "video/x-msvideo"
                         << "video/quicktime" << "video/vnd.rn-realvideo"
                         << "video/x-ms-afs" << "video/x-ms-asf"
                         << "video/x-ms-wmv" << "video/x-ms-wmx"
                         << "video/x-ms-wvxvideo" << "video/x-avi"
                         << "video/x-fli" << "video/x-flv" << "video/x-theora"
                         << "video/x-matroska" << "video/mp2t";
}

/******************************************************************************/

struct Player::Data {
    double volume = 1.0;
    MainWindow *mw = nullptr;
    PlayEngine *engine = nullptr;
    PlaylistModel *playlist = nullptr;
    QString playbackStatus;
    QVariantMap metaData;
    auto toDBus(PlayEngine::State state) -> QString
    {
        switch (state) {
        case PlayEngine::Playing:
            return u"Playing"_q;
        case PlayEngine::Paused:
        case PlayEngine::Buffering:
            return u"Paused"_q;
        default:
            return u"Stopped"_q;
        }
    }
    auto toDBus(const MetaData &md) const -> QVariantMap
    {
        QVariantMap map;
        const QDBusObjectPath path(dbusTrackId(md.mrl()));
        map["mpris:trackid"] = QVariant::fromValue(path);
        map["mpris:length"] = 1000LL*(qint64)md.duration();
        map["xesam:url"] = md.mrl().toString();
        map["xesam:title"] = md.title().isEmpty() ? engine->mediaInfo()->name()
                                                  : md.title();
        if (!md.album().isEmpty())
            map["xesam:album"] = md.album();
        if (!md.artist().isEmpty())
            map["xesam:artist"] = QStringList(md.artist());
        if (!md.genre().isEmpty())
            map["xesam:genre"] = QStringList(md.genre());
        return map;
    }
};

Player::Player(QObject *parent)
    : QDBusAbstractAdaptor(parent)
    , d(new Data)
{
    d->mw = getMainWindow();
    d->engine = d->mw->engine();
    d->playlist = d->mw->playlist();

    d->playbackStatus = d->toDBus(d->engine->state());
    d->volume = d->engine->volume();
    d->metaData = d->toDBus(d->engine->metaData());
    connect(d->engine, &PlayEngine::metaDataChanged, this, [this] () {
        d->metaData = d->toDBus(d->engine->metaData());
        sendPropertiesChanged(this, "Metadata", d->metaData);
    });
    connect(d->engine, &PlayEngine::stateChanged, this,
            [this] (PlayEngine::State state) {
        d->playbackStatus = d->toDBus(d->engine->state());
        QVariantMap map;
        map["PlaybackStatus"] = d->playbackStatus;
        map["CanPause"] = map["CanPlay"] = state != PlayEngine::Error;
        sendPropertiesChanged(this, map);
    });
    connect(d->engine, &PlayEngine::speedChanged, this, [this] (double speed) {
        sendPropertiesChanged(this, "Rate", speed);
    });
    auto checkNextPrevious = [this] () {
        QVariantMap map;
        map["CanGoNext"] = d->playlist->hasNext();
        map["CanGoPrevious"] = d->playlist->hasPrevious();
        sendPropertiesChanged(this, map);
    };
    connect(d->playlist, &PlaylistModel::loadedChanged,
            this, checkNextPrevious);
    connect(d->engine, &PlayEngine::seekableChanged, this,
            [this] (bool seekable) {
        sendPropertiesChanged(this, "CanSeek", seekable);
    });
    connect(d->engine, &PlayEngine::sought, this,
            [this] () { emit Seeked(time()); });
}

Player::~Player() {
    delete d;
}

auto Player::playbackStatus() const -> QString
{
    return d->playbackStatus;
}

auto Player::loopStatus() const -> QString
{
    return u"None"_q;
}

auto Player::setLoopStatus(const QString &/*status*/) -> void
{

}

auto Player::rate() const -> double
{
    return d->engine->speed();
}

auto Player::setRate(double rate) -> void
{
    d->engine->setSpeed(rate);
}

auto Player::isShuffled() const -> bool
{
    return false;
}

auto Player::setShuffled(bool /*s*/) -> void
{

}

auto Player::metaData() const -> QVariantMap
{
    return d->metaData;
}

auto Player::volume() const -> double
{
    return d->volume;
}

auto Player::setVolume(double volume) -> void
{
    if (_Change(d->volume, qBound(0.0, volume, 1.0))) {
        d->engine->setVolume(d->volume*100 + 0.5);
        sendPropertiesChanged(this, "Volume", d->volume);
    }
}

auto Player::time() const -> qint64
{
    const auto length = d->engine->time() - d->engine->begin();
    return qBound<qint64>(0LL, length, d->engine->duration()) * 1000LL;
}

auto Player::hasNext() const -> bool
{
    return d->playlist->hasNext();
}

auto Player::hasPrevious() const -> bool
{
    return d->playlist->hasPrevious();
}

auto Player::isPlayable() const -> bool
{
    return d->engine->state() != PlayEngine::Error;
}

auto Player::isPausable() const -> bool
{
    return d->engine->state() != PlayEngine::Error;
}

auto Player::isSeekable() const -> bool
{
    return d->engine->isSeekable();
}

auto Player::Next() -> void
{
    if (d->playlist->hasNext())
        d->playlist->playNext();
    else
        d->engine->stop();
}

auto Player::Previous() -> void
{
    if (d->playlist->hasPrevious())
        d->playlist->playPrevious();
    else
        d->engine->stop();
}

auto Player::Pause() -> void
{
    d->engine->pause();
}

auto Player::PlayPause() -> void
{
    d->mw->togglePlayPause();
}

auto Player::Stop() -> void
{
    d->engine->stop();
}

auto Player::Play() -> void
{
    d->mw->play();
}

auto Player::Seek(qint64 offset) -> void
{
    offset /= 1000;
    const auto duration = offset + d->engine->time() - d->engine->begin();
    if (d->engine->duration() > 0 && duration > d->engine->duration())
        Next();
    else
        d->engine->relativeSeek(offset);
}

auto Player::OpenUri(const QString &url) -> void
{
    d->mw->openFromFileManager(Mrl(QUrl(url)));
}

auto Player::SetPosition(const QDBusObjectPath &track, qint64 position) -> void
{
    position /= 1000;
    if (track.path() == dbusTrackId(d->engine->mrl()))
        d->engine->seek(position + d->engine->begin());
}

}