// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QTextCodec>
#include <functional>

namespace TextSaveGuard {
constexpr qint64 MaxBytes=64*1024*1024;
struct Snapshot { bool valid=false; bool exists=false; QByteArray digest; };
inline Snapshot fromBytes(const QByteArray &bytes) {return {true,true,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)};}
inline Snapshot capture(const QString &path,QString *error=nullptr) {
    const QFileInfo info(path);
    if(info.isSymLink()){if(error)*error=QStringLiteral("Refusing symbolic-link save target");return {};}
    if(!info.exists())return {true,false,{}};
    if(!info.isFile()||info.size()>MaxBytes){if(error)*error=QStringLiteral("Save target must be a regular file up to 64 MiB");return {};}
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){if(error)*error=file.errorString();return {};}
    const QByteArray bytes=file.read(MaxBytes+1);
    if(file.error()!=QFileDevice::NoError||bytes.size()>MaxBytes){if(error)*error=QStringLiteral("Unable to fingerprint save target");return {};}
    return fromBytes(bytes);
}
inline bool same(const Snapshot &a,const Snapshot &b) {return a.valid&&b.valid&&a.exists==b.exists&&(!a.exists||a.digest==b.digest);}
inline bool validUnicode(const QString &text) {
    for(int i=0;i<text.size();++i){const QChar c=text.at(i);if(c.isHighSurrogate()){if(i+1>=text.size()||!text.at(i+1).isLowSurrogate())return false;++i;}else if(c.isLowSurrogate())return false;}return true;
}
inline QByteArray codecName(const QString &encoding) {
    if(encoding==QStringLiteral("UTF-16LE-BOM"))return "UTF-16LE";
    if(encoding==QStringLiteral("UTF-16BE-BOM"))return "UTF-16BE";
    return encoding.toLatin1();
}
inline bool validDecode(const QByteArray &bytes,const QString &encoding) {
    QTextCodec *codec=QTextCodec::codecForName(codecName(encoding));if(!codec)return false;
    QTextCodec::ConverterState state;const QString text=codec->toUnicode(bytes.constData(),bytes.size(),&state);
    return state.invalidChars==0&&state.remainingChars==0&&validUnicode(text);
}
inline bool encode(const QString &text,const QString &encoding,bool utf8Bom,QByteArray &out,QString &error) {
    if(!validUnicode(text)){error=QStringLiteral("Text contains an unpaired Unicode surrogate");return false;}
    QTextCodec *codec=QTextCodec::codecForName(codecName(encoding));
    if(!codec){error=QStringLiteral("Unknown text encoding");return false;}
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    QByteArray bytes=codec->fromUnicode(text.constData(),text.size(),&state);
    if(state.invalidChars||state.remainingChars){error=QStringLiteral("Text cannot be represented in the selected encoding");return false;}
    if(encoding==QStringLiteral("UTF-16LE-BOM"))bytes.prepend(QByteArray::fromHex("fffe"));
    else if(encoding==QStringLiteral("UTF-16BE-BOM"))bytes.prepend(QByteArray::fromHex("feff"));
    else if(utf8Bom&&codec->name().toUpper()==QByteArray("UTF-8"))bytes.prepend(QByteArray::fromHex("efbbbf"));
    if(bytes.size()>MaxBytes){error=QStringLiteral("Saved text exceeds 64 MiB limit");return false;}
    out=bytes;return true;
}
inline bool saveAtomic(const QString &path,const QByteArray &bytes,const Snapshot &expected,QString &error,const std::function<void()> &beforeCommit={}) {
    if(bytes.size()>MaxBytes){error=QStringLiteral("Saved text exceeds 64 MiB limit");return false;}
    if(!expected.valid){error=QStringLiteral("No trusted file snapshot; reload before saving");return false;}
    const Snapshot initial=capture(path,&error);
    if(!same(initial,expected)){error=QStringLiteral("File changed outside the editor; reload or save to a new path");return false;}
    QSaveFile file(path);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}
    if(file.write(bytes)!=bytes.size()||!file.flush()){error=file.errorString();file.cancelWriting();return false;}
    if(beforeCommit)beforeCommit();
    if(!same(capture(path,&error),expected)){error=QStringLiteral("File changed during save; original preserved");file.cancelWriting();return false;}
    if(!file.commit()){error=file.errorString();return false;}
    return true;
}
}
