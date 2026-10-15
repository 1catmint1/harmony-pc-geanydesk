// SPDX-License-Identifier: GPL-2.0-or-later
#include "TextSaveGuard.h"
#include <QDir>
#include <QTemporaryDir>
#include <QFileInfo>
#include <cstdio>
#include <functional>
#include <stdexcept>
using namespace TextSaveGuard;
static void require(bool value){if(!value)throw std::runtime_error("assertion failed");}
static QByteArray read(const QString &p){QFile f(p);require(f.open(QIODevice::ReadOnly));return f.readAll();}
static void write(const QString &p,const QByteArray &b){QFile f(p);require(f.open(QIODevice::WriteOnly|QIODevice::Truncate));require(f.write(b)==b.size());}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 const QString root=QString::fromUtf8(argv[1]);
 if(!QFileInfo(root).isDir()||!QFileInfo(root).isAbsolute())return 3;
 QTemporaryDir dir(root+QStringLiteral("/case-XXXXXX"));if(!dir.isValid())return 4;
 int total=0,passed=0;
 auto test=[&](const char *name,const std::function<void()> &fn){++total;try{fn();++passed;std::printf("CONTROL %s PASS\n",name);}catch(const std::exception &e){std::printf("CONTROL %s FAIL %s\n",name,e.what());}};
 const QString unicode=QString::fromUtf8("中文😀\r\n");QString error;QByteArray out;
 test("utf8-crlf",[&]{require(encode(unicode,"UTF-8",false,out,error));require(out==unicode.toUtf8());});
 test("utf8-bom",[&]{require(encode(unicode,"UTF-8",true,out,error));require(out==QByteArray::fromHex("efbbbf")+unicode.toUtf8());});
 test("utf16le-bom",[&]{require(encode(QStringLiteral("A\r\n"),"UTF-16LE-BOM",false,out,error));require(out==QByteArray::fromHex("fffe41000d000a00"));});
 test("utf16be-bom",[&]{require(encode(QStringLiteral("A\r\n"),"UTF-16BE-BOM",false,out,error));require(out==QByteArray::fromHex("feff0041000d000a"));});
 test("utf16-no-bom",[&]{require(encode(QStringLiteral("A"),"UTF-16LE",false,out,error));require(out==QByteArray::fromHex("4100"));});
 test("latin1-valid",[&]{require(encode(QString::fromUtf8("café"),"ISO-8859-1",false,out,error));require(out==QByteArray::fromHex("636166e9"));});
 test("unrepresentable-not-substituted",[&]{out="preserve";require(!encode(unicode,"ISO-8859-1",false,out,error));require(out=="preserve");});
 test("unknown-encoding",[&]{require(!encode(unicode,"no-such-codec-xyz",false,out,error));});
 test("unpaired-surrogate",[&]{QString s;s.append(QChar(0xd800));require(!encode(s,"UTF-8",false,out,error));});
 test("invalid-utf8",[&]{require(!validDecode(QByteArray::fromHex("c0af"),"UTF-8"));});
 test("truncated-utf8",[&]{require(!validDecode(QByteArray::fromHex("e4b8"),"UTF-8"));});
 test("odd-utf16",[&]{require(!validDecode(QByteArray::fromHex("fffe4100ff"),"UTF-16LE-BOM"));});
 test("valid-unicode-decode",[&]{require(validDecode(unicode.toUtf8(),"UTF-8"));});
 test("valid-bom-decode",[&]{require(validDecode(QByteArray::fromHex("feff0041"),"UTF-16BE-BOM"));});
 const QString file=dir.path()+QStringLiteral("/中文-file.txt");
 test("new-file-atomic",[&]{require(saveAtomic(file,"original",capture(file),error));require(read(file)=="original");});
 test("existing-file-atomic",[&]{require(saveAtomic(file,"modified",capture(file),error));require(read(file)=="modified");});
 test("same-size-external-change",[&]{const Snapshot before=capture(file);write(file,"external");require(!saveAtomic(file,"new data",before,error));require(read(file)=="external");});
 test("changed-during-save",[&]{const Snapshot before=capture(file);require(!saveAtomic(file,"our bytes",before,error,[&]{write(file,"competitor");}));require(read(file)=="competitor");});
 test("external-created-file",[&]{const QString p=dir.path()+"/created";const Snapshot before=capture(p);write(p,"external");require(!saveAtomic(p,"ours",before,error));require(read(p)=="external");});
 test("external-deleted-file",[&]{const Snapshot before=capture(file);require(QFile::remove(file));require(!saveAtomic(file,"ours",before,error));require(!QFileInfo::exists(file));});
 test("unknown-baseline-refused",[&]{require(!saveAtomic(file,"ours",{},error));require(!QFileInfo::exists(file));});
 test("directory-target-refused",[&]{require(!capture(dir.path()).valid);require(!saveAtomic(dir.path(),"ours",{true,false,{}},error));});
 test("empty-file-save",[&]{require(saveAtomic(file,QByteArray(),capture(file),error));require(read(file).isEmpty());});
 test("symlink-target-refused",[&]{QString link=dir.path()+"/symlink";
#if defined(Q_OS_WIN)
 link+=QStringLiteral(".lnk");
#endif
 require(QFile::link(file,link));require(!capture(link).valid);require(!saveAtomic(link,"ours",fromBytes({}),error));require(read(file).isEmpty());});
 test("size-limit-before-read",[&]{const QString p=dir.path()+"/large";QFile f(p);require(f.open(QIODevice::WriteOnly));require(f.resize(MaxBytes+1));f.close();require(!capture(p).valid);require(QFile::remove(p));});
 test("failed-save-temporary-cleanup",[&]{const QStringList names=QDir(dir.path()).entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot);require(names.size()==3);});
 std::printf("SUMMARY {\"total\":%d,\"passed\":%d,\"failed\":%d,\"skipped\":0}\n",total,passed,total-passed);
 return passed==total?0:1;
}
