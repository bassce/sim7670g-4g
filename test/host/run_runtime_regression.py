"""Test the real runtime logger and atomic JSON writer on Windows only."""
from pathlib import Path
import subprocess
import ziglang

root=Path(__file__).resolve().parents[2]
out=root.parent/'outputs/runtime-regression';out.mkdir(parents=True,exist_ok=True)
stub=out/'stubs';(stub/'freertos').mkdir(parents=True,exist_ok=True)
(stub/'Arduino.h').write_text(r'''
#pragma once
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
inline uint32_t millis(){return 42;}
struct FakeSerial {
    std::vector<std::string> lines;
    void printf(const char* fmt,...) {char out[512];va_list args;va_start(args,fmt);vsnprintf(out,sizeof(out),fmt,args);va_end(args);lines.push_back(out);}
    void println(const char* line){lines.push_back(line);}
};
inline FakeSerial Serial;
''')
(stub/'freertos/FreeRTOS.h').write_text(r'''
#pragma once
#include <cstdint>
constexpr int pdTRUE=1,pdPASS=1;
constexpr uint32_t portMAX_DELAY=0xFFFFFFFF;
inline void(*testWriter)(void*)=nullptr;
inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*,int,void*,int,void*,int){testWriter=f;return pdPASS;}
''')
(stub/'freertos/queue.h').write_text(r'''
#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct FakeQueue {size_t limit,size;std::deque<std::vector<char>> records;};
using QueueHandle_t=FakeQueue*;
inline int allocations=0;
inline QueueHandle_t testQueue=nullptr;
inline QueueHandle_t xQueueCreate(size_t n,size_t size){++allocations;return testQueue=new FakeQueue{n,size,{}};}
inline void vQueueDelete(QueueHandle_t q){delete q;}
inline int xQueueSend(QueueHandle_t q,const void* data,int){if(q->records.size()==q->limit)return 0;q->records.emplace_back(static_cast<const char*>(data),static_cast<const char*>(data)+q->size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* out,uint32_t){if(q->records.empty())throw 1;memcpy(out,q->records.front().data(),q->size);q->records.pop_front();return 1;}
inline size_t uxQueueMessagesWaiting(QueueHandle_t q){return q->records.size();}
''')
logger=r'''
#include <cassert>
#include "BLETrace.cpp"
void drain(){try{testWriter(nullptr);}catch(int){}}
int main(){
    int sideEffects=0;
    BT_TRACE("TEST","%d",++sideEffects);BT_BYTES("RX",nullptr,++sideEffects);
    assert(sideEffects==0&&allocations==0&&BLETrace::stats().dropped==0);
    BLETrace::setEnabled(true);assert(BLETrace::enabled()&&allocations==1);drain();
    unsigned char payload[300];memset(payload,'A',sizeof(payload));
    BT_BYTES("RX",payload,sizeof(payload));assert(BLETrace::stats().queued==9);drain();
    bool truncated=false;for(const auto& line:Serial.lines)if(line.find("payload_truncated=44")!=std::string::npos)truncated=true;assert(truncated);
    for(int i=0;i<70;i++)BT_TRACE("TEST","record=%d",i);
    assert(BLETrace::stats().queued==64&&BLETrace::stats().dropped==6);
    BLETrace::setEnabled(false);auto printed=Serial.lines.size();drain();assert(Serial.lines.size()==printed);
    BT_TRACE("TEST","%d",++sideEffects);assert(sideEffects==0&&BLETrace::stats().dropped==6);
    BLETrace::setEnabled(true);BT_TRACE("OLD_SESSION","old");BLETrace::setEnabled(false);BLETrace::setEnabled(true);drain();
    for(const auto& line:Serial.lines)assert(line.find("OLD_SESSION")==std::string::npos);
    for(int i=0;i<100;i++){BLETrace::setEnabled(false);BLETrace::setEnabled(true);drain();}
    assert(allocations==1);BLETrace::setEnabled(false);
    puts("PASS: runtime debug gating, disabled argument cost, bounded queue/payload, stale-session discard, 100 toggles without queue reallocation");
    delete testQueue;
}
'''
(out/'logger-test.cpp').write_text(logger)
(stub/'FS.h').write_text(r'''
#pragma once
#include <map>
#include <string>
#include <algorithm>
using String=std::string;
constexpr int FILE_WRITE=1;
inline size_t writeLimit=100000;
struct File {
    std::string* data=nullptr;
    explicit operator bool() const{return data!=nullptr;}
    size_t write(const char* text,size_t size){size=std::min(size,writeLimit);data->append(text,size);return size;}
    void flush(){} void close(){} size_t size()const{return data->size();}
};
namespace fs {struct FS {
    std::map<std::string,std::string> files;
    bool failOpen=false,failRename=false;
    File open(const String& name,int){if(failOpen)return {};files[name].clear();return {&files[name]};}
    bool rename(const String& src,const String& dest){if(failRename)return false;files[dest]=files[src];files.erase(src);return true;}
    void remove(const String& path){files.erase(path);}
};}
''')
(stub/'ArduinoJson.h').write_text(r'''
#pragma once
#include "FS.h"
struct JsonDocument {std::string text;};
inline size_t measureJson(const JsonDocument& doc){return doc.text.size();}
inline size_t serializeJson(const JsonDocument& doc,File& file){return file.write(doc.text.data(),doc.text.size());}
''')
(out/'file-test.cpp').write_text(r'''
#include <cassert>
#include <cstdio>
#include "AtomicJsonFile.h"
int main(){
    fs::FS fs;fs.files["/settings.json"]="original";JsonDocument doc{"{\"debug\":true}"};
    fs.failOpen=true;assert(!writeJsonAtomic(fs,"/settings.json",doc));assert(fs.files["/settings.json"]=="original");fs.failOpen=false;
    writeLimit=2;assert(!writeJsonAtomic(fs,"/settings.json",doc));assert(fs.files["/settings.json"]=="original"&&fs.files.count("/settings.json.tmp")==0);writeLimit=100000;
    fs.failRename=true;assert(!writeJsonAtomic(fs,"/settings.json",doc));assert(fs.files["/settings.json"]=="original"&&fs.files.count("/settings.json.tmp")==0);fs.failRename=false;
    assert(writeJsonAtomic(fs,"/settings.json",doc));assert(fs.files["/settings.json"]==doc.text&&fs.files.count("/settings.json.tmp")==0);
    puts("PASS: original configuration survives open, partial-write and rename failures; complete atomic replacement succeeds");
}
''')
zig=Path(ziglang.__file__).parent/'zig.exe'
for name in ['logger','file']:
    exe=out/(name+'-test.exe')
    command=[str(zig),'c++','-std=c++17','-target','x86_64-windows-gnu','-O0','-g','-I',str(stub),'-I',str(root/'src'),'-I',str(root/'lib/BLETrace/src')]
    if name=='logger':command+=['-DBLE_DIAGNOSTICS=1']
    subprocess.run(command+[str(out/(name+'-test.cpp')),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
