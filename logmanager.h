#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QObject>
#include <QDateTime>
#include "plcmanager.h"

class LogCatcher : public QObject{
    Q_OBJECT
public:
    enum Source{
        App,
        PLC
    };
    Q_ENUM(Source);
    enum Level{
        Critical,
        Alarm,
        Warning,
        Info,
        Debug
    };
    Q_ENUM(Level);
    enum Wrapped{
        wrapNo,
        wrapYes
    };
    Q_ENUM(Wrapped);
    enum TimeType{
        TimeReal,
        TimeUptime
    };
    Q_ENUM(TimeType);
signals:
    void newLogEntry(const QDateTime &timestamp,
                     LogCatcher::Source source,
                     LogCatcher::Level level,
                     const QString &message,
                     const plcManager::CommandContext &ctx,
                     LogCatcher::Wrapped wrap = LogCatcher::wrapNo,
                     LogCatcher::TimeType timeType = LogCatcher::TimeReal);
};

class LogManager : public QObject
{
    Q_OBJECT
public:

    using Source = LogCatcher::Source;
    using Level = LogCatcher::Level;
    using Wrapped = LogCatcher::Wrapped;
    using TimeType = LogCatcher::TimeType;

    LogManager(const plcManager::CommandContext &ctx, bool parse, Source source);
    LogManager(Source source, Level level = Level::Info, Wrapped wrap = Wrapped::wrapNo);
    LogManager(const plcManager::CommandContext &ctx, Source source, Level level = Level::Info, Wrapped wrap = Wrapped::wrapNo);
    // LogManager(const QDateTime &timestamp, Source source, Level level = Level::Info, Wrapped wrap = Wrapped::wrapNo);
    ~LogManager();

    template <typename T>
    LogManager& operator<<(const T& value) {
        QDebug(&m_buffer).noquote() << value;
        return *this;
    }

    static LogCatcher* catcher();

    void setLoglevel(const Level &newLoglevel);

private:
    Source m_source;
    Level m_level;
    Wrapped m_wrap;
    QDateTime m_timestamp;
    QString m_buffer;
    TimeType m_timetype;
    bool m_parse = false;
    Level loglevel = LogCatcher::Debug;
    plcManager::CommandContext m_ctx;
};

inline LogManager logApp(LogCatcher::Level level = LogCatcher::Info) {
    return LogManager(LogManager::Source::App, level);
}

inline LogManager logPLC(const QString &name = QString(), LogCatcher::Level level = LogCatcher::Info,
                         LogCatcher::Wrapped wrap = LogCatcher::wrapNo) {
    plcManager::CommandContext ctx;
    ctx.target.name = name;
    return LogManager(ctx, LogCatcher::PLC, level, wrap);
}


inline LogManager debugPLC(const QString &name = QString()) {
    plcManager::CommandContext ctx;
    ctx.target.name = name;
    return LogManager(ctx, LogCatcher::PLC, LogCatcher::Debug);
}

inline LogManager debugApp() {
    return LogManager(LogCatcher::App, LogCatcher::Debug);
}

inline LogManager rawPLC(const plcManager::CommandContext &ctx){
    return LogManager(ctx, true, LogCatcher::PLC);
}

#endif // LOGMANAGER_H
