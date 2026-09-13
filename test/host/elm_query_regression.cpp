#include "ELMduino.h"
#include <cassert>
#include <deque>
#include <functional>
#include <iostream>

struct Adapter : Stream {
    std::deque<char> input;
    std::vector<std::string> commands;
    std::string command, response = "62000C0028\r>", protocol="A6";
    std::string reject, deferred;
    uint32_t releaseAt=0;
    bool silent=false;
    void feed(const std::string& text) { for(char c:text)input.push_back(c); }
    int available() override {
        if (!deferred.empty() && hostMillis >= releaseAt) {feed(deferred);deferred.clear();}
        return input.size();
    }
    int read() override {if(!available())return -1;char c=input.front();input.pop_front();return c;}
    size_t write(uint8_t c) override {
        if(c!='\r'){command+=char(c);return 1;}
        commands.push_back(command);
        if(command==reject&&!reject.empty())feed("?\r>");
        else if(command.empty())feed("STOPPED\r>");
        else if(command=="AT DPN")feed(protocol+"\r>");
        else if(command=="AT Z")feed("ELM327 v2.3\r>");
        else if(command.rfind("AT",0)==0)feed("OK\r>");
        else if(command=="0100"||command=="01001")feed("4100BE3FB813\r>");
        else if(!silent)feed(response);
        command.clear();return 1;
    }
};
double query(ELM327& elm,uint8_t sid,uint16_t did,uint8_t bytes=2,
             uint8_t offset=0,uint8_t length=0,bool isSigned=false,double scale=1,uint8_t responses=1) {
    double value=elm.processPID(sid,did,responses,bytes,scale,0,offset,length,isSigned);
    unsigned steps=0;
    while(elm.nb_rx_state==ELM_GETTING_MSG) {assert(++steps<50000);value=elm.processPID(sid,did,responses,bytes,scale,0,offset,length,isSigned);}
    return value;
}
int main() {
    Adapter serial;ELM327 elm;
    assert(elm.begin(serial,false,2000,'0',1024));
    elm.specifyNumResponses=false;
    assert(query(elm,0x22,0x000C)==40 && elm.nb_rx_state==ELM_SUCCESS);
    assert(serial.commands.back()=="22000C"); // Not 220C and not the engine-RPM formula.
    serial.response="410C0028\r>";
    assert(query(elm,1,0x0C)==10 && elm.nb_rx_state==ELM_SUCCESS);
    serial.response="7F0111\r410C0028\r>";
    assert(query(elm,1,0x0C)==10 && elm.nb_rx_state==ELM_SUCCESS);
    serial.response="7F0111\r>";
    query(elm,1,0x0C);assert(elm.nb_rx_state==ELM_GENERAL_ERROR&&elm.lastNRC==0x11);
    serial.response="62000C0028\r62000C0029\r>";
    query(elm,0x22,0x000C);assert(elm.responseError==ELMResponse::Error::ambiguous);
    serial.response="62000C0028\r62000C0028\r>";
    assert(query(elm,0x22,0x000C)==40&&elm.nb_rx_state==ELM_SUCCESS);
    for(const auto& bad:{"62000C28\r>","62000C000028\r>","62000C2G\r>","62000C0\r>","62000D0028\r>"}) {
        serial.response=bad;query(elm,0x22,0x000C);assert(elm.nb_rx_state!=ELM_SUCCESS);
    }
    serial.response="009\r0:620100010203\r1:040506\r>";
    assert(query(elm,0x22,0x0100,6)==double(0x010203040506ULL)&&elm.nb_rx_state==ELM_SUCCESS);
    for(const auto& bad:{"009\r0:620100010203\r>","009\r0:620100010203\r2:040506\r>","0:620100010203\r>","FFF\r0:620100010203\r>","009\r0:620100010203\r1:0405G6>"}) {
        serial.response=bad;query(elm,0x22,0x0100,6);assert(elm.nb_rx_state!=ELM_SUCCESS);
    }
    serial.response="7F2278\r62000C0028\r>";
    assert(query(elm,0x22,0x0C)==40&&elm.nb_rx_state==ELM_SUCCESS);
    serial.response="7F2278\r>";
    query(elm,0x22,0x0C);assert(elm.responseError==ELMResponse::Error::pending&&elm.lastNRC==0x78);
    serial.response="7F2278\r";
    serial.deferred="62000C0028\r>";serial.releaseAt=hostMillis+3500;
    assert(query(elm,0x22,0x0C)==40&&elm.nb_rx_state==ELM_SUCCESS); // Wait past the normal 2 s window.
    serial.silent=true;query(elm,0x22,0x0C);assert(elm.nb_rx_state==ELM_TIMEOUT);
    serial.silent=false;serial.response="62000C0029\r>";
    assert(query(elm,0x22,0x0C)==41&&elm.nb_rx_state==ELM_SUCCESS);
    assert(serial.commands[serial.commands.size()-2].empty()); // Abort and drain before new request.
    assert(elm.prepareHeader(0x7E4));assert(serial.commands.back()=="AT SH 7E4");
    assert(elm.restoreHeader());assert(serial.commands.back()=="AT SH 7DF");
    const auto count=serial.commands.size();assert(!elm.prepareHeader(0x18DA10F1));assert(serial.commands.size()==count);
    serial.reject="AT SH 7E4";assert(!elm.prepareHeader(0x7E4));
    serial.reject="AT SH 7DF";assert(!elm.prepareHeader(0));
    serial.reject.clear();assert(elm.prepareHeader(0));
    Adapter can29;can29.protocol="A7";ELM327 other;assert(other.begin(can29,false,2000,'7',1024));
    assert(other.prepareHeader(0x18DA10F1));assert(can29.commands.back()=="AT SH 18DA10F1");
    assert(other.restoreHeader());assert(can29.commands.back()=="AT SH 18DB33F1");
    Adapter legacy;legacy.protocol="3";ELM327 old;assert(old.begin(legacy,false,2000,'3',1024));
    assert(old.prepareHeader(0x686AF1));assert(old.restoreHeader());
    assert(std::find(legacy.commands.begin(),legacy.commands.end(),"AT E0")!=legacy.commands.end());
    Adapter invalid;invalid.reject="AT H0";ELM327 failed;assert(!failed.begin(invalid,false,2000,'6',1024));
    serial.response="014\r0:490201314434\r1:47503030523535\r2:42313233343536\r>";
    char vin[18]={};assert(elm.get_vin_blocking(vin)==ELM_SUCCESS);assert(std::string(vin)=="1D4GP00R55B123456");
    serial.response="49020131\r>";assert(elm.get_vin_blocking(vin)!=ELM_SUCCESS&&vin[0]==0);
    std::vector<std::string> decoded;
    std::string large="200\r0:"+std::string(300,'0');
    assert(ELMResponse::messages(large.c_str(),1025,decoded)!=ELMResponse::Error::none);
    // Recorded Car Scanner messages, in the firmware's ATH0/CAF1 format.
    ELMQueryConfig meb;meb.protocol=7;meb.receiveHeader=0x17FE007B;meb.flowControlHeader=0x17FC007B;
    can29.commands.clear();
    assert(other.prepareQuery(0x17FC007B,meb));
    assert((can29.commands==std::vector<std::string>{"AT SH 17FC007B","AT CRA 17FE007B","AT FCSH 17FC007B","AT FCSD 300000","AT FCSM 1"}));
    other.specifyNumResponses=true;
    can29.response="621E3B0626\r>";
    assert(query(other,0x22,0x1E3B,2,0,0,false,0.25)==393.5);
    assert(can29.commands.back()=="221E3B1");
    assert(other.restoreHeader());
    assert(can29.commands[can29.commands.size()-3]=="AT FCSM 0");
    assert(can29.commands[can29.commands.size()-2]=="AT AR");
    assert(can29.commands.back()=="AT SH 18DB33F1");
    assert(other.prepareQuery(0x17FC007B,meb));
    can29.response="62028CF0\r>";
    assert(query(other,0x22,0x028C,1,0,0,false,0.4)==96.0);
    assert(other.restoreHeader());
    can29.response="7F0111\r415BF4\r>";
    assert(std::abs(query(other,1,0x5B,1,0,0,false,100.0/255)-95.6862745098)<0.00001);
    assert(can29.commands.back()=="015B"); // No premature 1-response stop on a broadcast.
    ELMQueryConfig battery;battery.protocol=7;battery.receiveHeader=0x18DAF105;
    assert(other.prepareQuery(0x18DB33F1,battery));
    for (const auto& frame : {"008\r0:419A0600FFFF\r1:FFF7AAAAAAAAAA\r>",
                              "7F0111\r008\r0:419A0600FFFF\r1:FFF7\r>",
                              "008\r0:419A0600FFFF\r7F0111\r1:FFF7\r>"}) {
        can29.response=frame;
        assert(std::abs(query(other,1,0x9A,6,4,2,true,0.1)+0.9)<0.000001);
        assert(other.nb_rx_state==ELM_SUCCESS && can29.commands.back()=="019A");
    }
    assert(other.restoreHeader());
    double field=0;
    assert(ELMResponse::fieldValue(0x0600FFFFFFF7ULL,6,4,2,true,field)&&field==-9);
    assert(ELMResponse::fieldValue(UINT64_MAX,8,6,2,true,field)&&field==-1);
    assert(ELMResponse::fieldValue(uint64_t(1)<<63,8,0,8,true,field)&&field==-9223372036854775808.0);
    assert(!ELMResponse::fieldValue(0,6,5,2,true,field));
    for (auto value : {0x7FFF,0x8000,0xFFFF,0x0000}) {
        assert(ELMResponse::fieldValue(value,2,0,2,true,field));
        assert(field==(value >= 0x8000 ? value-65536 : value));
    }
    auto sent=can29.commands.size();
    query(other,1,0x9A,6,5,2,true);assert(other.nb_rx_state==ELM_GENERAL_ERROR&&can29.commands.size()==sent);
    // Failure at every setup stage must allow rollback and prohibit stale filters.
    for (const auto& command : {"AT SH 17FC007B","AT CRA 17FE007B","AT FCSH 17FC007B","AT FCSD 300000","AT FCSM 1"}) {
        can29.reject=command;
        assert(!other.prepareQuery(0x17FC007B,meb));
        can29.reject.clear();assert(other.restoreHeader());
        assert(can29.commands.back()=="AT SH 18DB33F1");
    }
    for (const auto& command : {"AT FCSM 0","AT AR","AT SH 18DB33F1"}) {
        assert(other.prepareQuery(0x17FC007B,meb));can29.reject=command;
        assert(!other.restoreHeader());assert(!other.prepareHeader(0));
        can29.reject.clear();assert(other.prepareHeader(0));
    }
    // 11 -> 29 -> 11 changes protocol, header, receive filter and FC as one transaction.
    serial.reject.clear();serial.commands.clear();
    assert(elm.prepareQuery(0x17FC007B,meb));
    assert(serial.commands.front()=="AT SP 7");
    assert(elm.restoreHeader());
    assert(serial.commands[serial.commands.size()-3]=="AT SP 6");
    assert(serial.commands.back()=="AT SH 7DF");
    serial.reject="AT SP 7";assert(!elm.prepareQuery(0x17FC007B,meb));
    serial.reject.clear();assert(elm.restoreHeader());
    assert(elm.prepareQuery(0x17FC007B,meb));serial.reject="AT SP 6";
    assert(!elm.restoreHeader()&&!elm.prepareHeader(0));serial.reject.clear();assert(elm.restoreHeader());
    meb.protocol=6;sent=serial.commands.size();assert(!elm.prepareQuery(0x17FC007B,meb));assert(serial.commands.size()==sent);
    meb.protocol=7;meb.flowControlData=0x300080;assert(!elm.prepareQuery(0x17FC007B,meb));
    std::cout<<"PASS: complete ELM driver, recorded SOC/current/UDS values, multi-ECU replies, protocol/filter/FC switching, all setup/restore failures, signed field bounds\n";
}
