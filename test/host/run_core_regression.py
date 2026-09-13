"""Run reviewed production methods against deterministic host hardware mocks.
This builds a Windows test executable only, never firmware or a flash image.
"""
from pathlib import Path
import re, subprocess
import ziglang

root=Path(__file__).resolve().parents[2]
out=root.parent/'outputs/core-regression';out.mkdir(parents=True,exist_ok=True)
def extract(path,signature):
    text=(root/path).read_text(encoding='utf-8')
    start=re.search(r'(?m)^'+re.escape(signature),text).start()
    opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'
source=Path(__file__).with_name('obd_core_stub.h').read_text()
for sig in ['OBDState::~OBDState()', 'uint32_t OBDState::supportedPIDs(', 'bool OBDState::isPIDSupported(',
            'void OBDState::setPayload(', 'void OBDState::setCheckPidSupport(']:
    source+=extract('src/OBDState.cpp',sig)
source+='template<class T>\n'+extract('src/OBDState.cpp','void TypedOBDState<T>::readValue()')
for sig in ['bool OBDStates::compareStates(', 'void OBDStates::clearStates(', 'void OBDStates::addState(',
            'double OBDStates::avgLastUpdate(', 'OBDState *OBDStates::nextState()']:
    source+=extract('src/OBDStates.cpp',sig)
source+='\n#include "RuntimeAccess.h"\n'
source+=r'''
int main() {
    ELM327 elm;
    TypedOBDState<int> state;state.elm327=&elm;
    for(auto [pid,base,mask]:std::vector<std::tuple<uint16_t,uint16_t,uint32_t>>{
        {1,0,0x80000000},{0x20,0,1},{0x21,0x20,0x80000000},{0x40,0x20,1},{0xA6,0xA0,1U<<26}}) {
        elm.steps={{ELM_SUCCESS,double(mask)}};
        assert(state.isPIDSupported(1,pid));assert((elm.queried.back()&0xFFFF)==base);
        elm.steps={{ELM_SUCCESS,0}};assert(!state.isPIDSupported(1,pid));
    }
    assert(state.isPIDSupported(0x22,0x1234));
    state.pid=0x21;state.setCheckPidSupport(true);elm.queried.clear();
    elm.steps={{ELM_GETTING_MSG,0},{ELM_SUCCESS,2147483648.0},{ELM_GETTING_MSG,0},{ELM_SUCCESS,42}};
    state.readValue();assert(state.processing&&!state.init&&elm.queried.size()==1);
    state.readValue();assert(state.init&&state.supported&&state.processing);
    state.readValue();assert(state.value==42&&!state.processing&&elm.steps.empty());
    state.setCheckPidSupport(true);elm.steps={{ELM_SUCCESS,0}};state.readValue();
    assert(state.init&&!state.supported&&!state.processing&&elm.steps.empty());
    state.setCheckPidSupport(true);elm.steps={{ELM_GENERAL_ERROR,0}};state.readValue();
    assert(!state.init&&state.supported&&!state.processing&&state.lastAttempt==nowMs);
    state.setCheckPidSupport(false);elm.steps={{ELM_SUCCESS,77}};state.readValue();assert(state.value==77);
    state.setPayload(nullptr);state.setPayload("saved");auto saved=state.payload;
    state.setPayload(saved);assert(state.payload==saved);
    failAllocation=true;state.setPayload("much longer new allocation");assert(state.payload==saved&&std::string(state.payload)=="saved");failAllocation=false;
    state.setPayload("new");assert(std::string(state.payload)=="new");
    TypedOBDState<int> active,idle;active.processing=true;active.lastUpdate=2000;idle.lastUpdate=0;
    assert(OBDStates::compareStates(&active,&idle));assert(!OBDStates::compareStates(&idle,&active));assert(!OBDStates::compareStates(&active,&active));
    OBDStates states;states.elm327=&elm;idle.enabled=false;states.states={&idle};assert(states.nextState()==nullptr);
    idle.enabled=true;idle.init=true;idle.supported=false;assert(states.nextState()==nullptr);
    idle.supported=true;idle.updateInterval=-1;idle.lastUpdate=100;assert(states.nextState()==nullptr);
    assert(states.avgLastUpdate([](OBDState*){return false;})==0.0);states.states.clear();
    int destroyed=0;
    struct Derived : OBDState {int* count;explicit Derived(int* n):count(n){}~Derived(){++*count;}};
    states.addState(new Derived(&destroyed));states.addState(new Derived(&destroyed));assert(destroyed==1);
    states.clearStates();assert(destroyed==2&&states.states.empty());
    // NO DATA and errors retain the last good value, but advance retry time.
    state.setCheckPidSupport(false);state.lastUpdate=123;state.value=77;
    elm.steps={{ELM_NO_DATA,0}};state.readValue();
    assert(state.value==77&&state.lastUpdate==123&&state.lastAttempt==nowMs&&!state.processing);
    elm.steps={{ELM_GENERAL_ERROR,0}};state.readValue();assert(state.value==77&&state.lastUpdate==123);
    // Header setup failure prevents any vehicle query; recovery failure prevents publishing it.
    const auto queries=elm.queried.size();elm.headerOK=false;state.header=0x7E4;state.readValue();
    assert(elm.queried.size()==queries&&state.updateStatus==ELM_GENERAL_ERROR&&!state.processing);
    elm.headerOK=true;elm.restoreOK=false;elm.steps={{ELM_SUCCESS,88}};state.readValue();
    assert(state.value==77&&state.lastUpdate==123&&state.updateStatus==ELM_GENERAL_ERROR);
    elm.restoreOK=true;state.header=0;
    // Unsigned 32-bit bitmaps preserve bit 31 in the signed display container.
    std::strcpy(state.valueFormatFunctionName,"toBitStr");
    elm.steps={{ELM_SUCCESS,2147483648.0}};state.readValue();
    assert(state.updateStatus==ELM_SUCCESS&&state.value==std::numeric_limits<int>::lowest());
    elm.steps={{ELM_SUCCESS,4294967295.0}};state.readValue();
    assert(state.updateStatus==ELM_SUCCESS&&state.value==-1);
    elm.steps={{ELM_SUCCESS,4294967296.0}};state.readValue();
    assert(state.updateStatus==ELM_GENERAL_ERROR&&state.value==-1);
    state.valueFormatFunctionName[0]=0;
    elm.steps={{ELM_SUCCESS,2147483648.0}};state.readValue();
    assert(state.updateStatus==ELM_GENERAL_ERROR&&state.value==-1);
    // A recently failed one-shot support check can retry; another due item gets a turn first.
    TypedOBDState<int> failed,ready;failed.name="failed";ready.name="ready";
    failed.updateInterval=-1;failed.lastAttempt=nowMs;ready.lastAttempt=nowMs-1000;
    ready.elm327=&elm;ready.setCheckPidSupport(false);states.states={&failed,&ready};
    elm.steps={{ELM_SUCCESS,22}};assert(states.nextState()==&ready);states.states.clear();
    { RuntimeAccess::Read a;RuntimeAccess::Read b;RuntimeAccess::Write writer;assert(a&&b&&!writer); }
    { RuntimeAccess::Write writer;RuntimeAccess::Read reader;RuntimeAccess::Write second;assert(writer&&!reader&&!second); }
    { RuntimeAccess::Read reader;assert(reader); }
    { RuntimeAccess::Read a;RuntimeAccess::Read b;RuntimeAccess::Upgrade upgrade;assert(!upgrade); }
    { RuntimeAccess::Read reader;RuntimeAccess::Upgrade upgrade;RuntimeAccess::Read other;assert(upgrade&&!other); }
    assert(RuntimeAccess::state()==0);
    puts("PASS: PID bitmap boundaries, async support checks, reconnect reset, scheduler, empty lists, resource cleanup, invalid ELM replies, configuration exclusion");
}
'''
cpp=out/'core-regression.cpp';cpp.write_text(source,encoding='utf-8')
zig=Path(ziglang.__file__).parent/'zig.exe';exe=out/'core-regression.exe'
subprocess.run([str(zig),'c++','-std=c++17','-target','x86_64-windows-gnu','-O0','-g','-I',str(root/'src'),'-I',str(root/'vendor/ELMDuino/src'),str(cpp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
