#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include "Machine.h"
#include "VirtualMachine.h"
#include <vector>
#include <queue>
#include <iostream>
#include <algorithm>
#include <string.h>
#include <map>
#include <stack>

using namespace std;
extern "C"{
    
volatile int tickCounter = 0;
volatile int tickConversion;
volatile bool mainCreated = false;
volatile int mutexIDCounter = 0;
volatile int threadIDCounter = 2;
volatile int memPoolIDCounter = 2;
const char * image;
string  cwd = "/"; 
char *longname = new char[256];
volatile int FATfd;
volatile int RootDirSectors;
volatile int FirstDataSector;
volatile int DataSec;
volatile int CountOfClusters;
volatile int FirstRootSector;
volatile int fileDescriptorCount = 3;
const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 0;
void * byteArrayStart;
void * heapArrayStart;
class Mutex;
int sharedSize;
int heapSize;
TVMMutexID readwriteMutex;

class memPool{
public:
    void * initialptr;
    TVMMemorySize size;
    TVMMemoryPoolID ID;
    bool * boolArray;
    map <void *, int> ptrSizes;
    memPool();
    memPool(void * init, TVMMemorySize siz, TVMMemoryPoolID eyedee);
    
};

class TCB{
public:
    SMachineContext context; 
    TVMThreadID ID; 
    TVMThreadPriority priority;
    TVMThreadState state;
    TVMMemorySize memSize;
    void * stack;
    TVMThreadEntry entry;
    TVMTick tick;
    bool sleeping;
    int result;
    Mutex * mutex;
    void * param;
    TCB();
    TCB(SMachineContext mContext, TVMThreadID id, TVMThreadPriority prio,
    TVMThreadState stat, TVMMemorySize mSize,TVMThreadEntry entry,void * para);
    //~TCB();
};

class Compare{
public:
    bool operator() (TCB *r,TCB *l){
        return (r->priority < l->priority);
    }
};

class Mutex{
public:
    bool locked;
    TVMMutexID ID;
    TVMThreadID owner;
    TCB * ownerTCB;
    priority_queue <TCB*, vector<TCB*>, Compare > waiting;
    Mutex(TVMMutexID id);
};

class BPB{
public:
    int BytsPerSec;
    int SecPerClus;
    int RsvdSecCnt;
    int NumFATs;
    int RootEntCnt;
    int TotSec16;
    int Media;
    int FATSz16;
    int SecPerTrk;
    int NumHeads;
    int HiddSec;
    int TotSec32;
    int DrvNum;
    int Reserved1;
    int BootSig;
    int VolID;
    int VolLab;
    BPB(uint8_t *buffer);
};

class RootEntry{
public:
    char* Name;
    const char *longName; 
    int Attr;
    int NTRes;
    int CrtTimeTenth;
    uint16_t * CrtTime;
    uint16_t * CrtDate;
    SVMDateTime createTime;
    SVMDateTime lastAccessed;
    uint16_t *  LstAccDate;
    int FstClusHI;
    uint16_t * WrtTime;
    uint16_t * WrtDate;
    SVMDateTime writeTime;
    int FstClusLO;
    int  FileSize;
    bool created;
    RootEntry(uint8_t *entry, char * longname);
    RootEntry();
    RootEntry(char * name, const char * longname, int attr, int nTRes,SVMDateTime crtTime, SVMDateTime lastAccess, SVMDateTime wrtTime, int FstClusHi, int FstClusLo, int size );
    
};

class File{
public:
    int FD;
    const char * fileName;
    int flags;
    int modes;
    string fileLocation;
    int clusterNO;
    int sectorNO;
    int entryNO;
    int fileptr;     
    RootEntry* correspondingEntry;
    bool isDirectory;
    RootEntry*** entries;
    File(int fileDescriptor, const char* name, int flag, int mode, string location, int cluster, int sector, int entry, RootEntry* corrEntry, bool isdirectory, RootEntry*** Entries);
    File(int fileDescriptor);
};



char * calculateShortName(const char * name){
    char * temp = new char[12];
    bool success = false;
    if (strlen(name) < 12){
        memcpy(temp,name, strlen(name));
        for (char * p = temp; *p != '\0';++p )
            *p = toupper(*p);
        return temp;
    }
    for (unsigned i = 0; i < strlen(name); i++)
    {
        if (name[i] == '.')
        {
            success = true;
            if (i <= 6)
            {
                memcpy(temp, name, strlen(name));
                for (char * p = temp; *p != '\0';++p )
                    *p = toupper(*p);
            }
            else if (i > 6)
            {
                memcpy(temp,name, 6);
                temp[6] = '~';
                temp[7] = '1';
                memcpy(temp+8, name+i, 3);
                for (char * p = temp; *p != '\0';++p )
                    *p = toupper(*p);
            }
        }
    }
    if (!success){
        if (strlen(name) > 12)
            memcpy(temp,name, 12);
        else
            memcpy(temp,name,strlen(name));
        for (char * p = temp; *p != '\0';++p )
            *p = toupper(*p);
    }
    return temp;
}

uint8_t * twoBytesToByte(uint16_t** bytes, int start){
//    uint8_t * array = new uint8_t[2];
//    memcpy(array+start, bytes, 1);
//    memcpy(array+start+1, bytes, 1);
//    return array;  
    uint8_t * array = new uint8_t[512];
    uint8_t * ptr = (uint8_t *) bytes+start;
    for (int i = 0; i < 512; i++)
    {
        array[i] = *ptr;
        ptr++;
    }
    
    return array;
    
}

uint16_t * byteToTwoBytes(uint8_t* bytes, int start, int end){
    uint16_t * temp = new uint16_t;
    //temp = ((uint16_t *)(bytes+(end-start)));
    memcpy(temp, bytes+start, 2);   
    return temp;
}

uint16_t extract(uint16_t bytes, int start, int end){
    uint16_t mask = (1 << (end-start+1))-1;
    return (bytes >> start) & mask;
}

unsigned char getNthBit(unsigned char c, unsigned char n)
{
  unsigned char tmp=1<<n;
  return (c & tmp)>>n;
}

unsigned char setNthBit(unsigned char c, unsigned char n) //set nth bit from right
{
  unsigned char tmp=1<<n;
  return c | tmp;
}

void inject(unsigned char Dwhatever, uint16_t * current, int start, int end)
{
    for (int i = start; i < end+1; i++){
        //int bit = (Dwhatever >> i-start) & 1;
        unsigned char bit  = getNthBit(Dwhatever, i-start);
      //*current ^= (-bit ^ Dwhatever) & (1 << i);
        if (bit == 1)
            *current = setNthBit(*current,i);
    }
}


int byteToInt(uint8_t* bytes, int start, int end){
    int result=0; 
    if (end - start == 4)
    {
        result = bytes[start]|bytes[start+1]<< 8|bytes[start+2]<<16|bytes[start+3]<<24;
    }
    else if (end - start == 2){
        result = bytes[start]|bytes[end-1] << 8;
    }
    else if (end - start == 1){
        result = (int) bytes[start];
    }
    return result;
}

uint8_t* intToByte(int number, int precision){
    uint8_t * temp = new uint8_t[precision];
    unsigned char *bytes = (unsigned char *) (&number);
    for (int i = 0; i < precision; i++)
    {
        memcpy(temp+i, bytes+i, 1);
    }
    return temp;
    
}

char * byteToChar(uint8_t* bytes, int start, int end){
    char * temp = new char[end-start+1];
    memcpy(temp, bytes+start, end-start);
    temp[end-start] = '\0';
    return temp;
}

char * unicodeToASCII(uint8_t* bytes, int start, int end)
{
    char * temp = new char[(end-start)/2 + 1];
    if ((int) bytes[start] == 255)
    {
        temp = "";
        return temp;
    }
    for (int i = 0; i < end-start; i+=2)
    {
        temp[i/2] = *(bytes+start+i);
    }
    temp[(end-start)/2] = '\0';
    return temp;
}


RootEntry::RootEntry(uint8_t* entry, char * longname){
    longName = longname;
    Name = calculateShortName(longname);
    Attr = byteToInt(entry, 11, 11+1);
    NTRes= byteToInt(entry, 12, 12+1);
    CrtTimeTenth = byteToInt(entry, 13, 14);
    CrtTime = byteToTwoBytes(entry, 14, 16);
    CrtDate = byteToTwoBytes(entry, 16, 18);
    
    LstAccDate = byteToTwoBytes(entry, 18, 20);
    FstClusHI = byteToInt(entry, 20, 22);
    WrtTime = byteToTwoBytes(entry,22,24);
    WrtDate = byteToTwoBytes(entry,24,26);
    FstClusLO = byteToInt(entry,26,28);
    FileSize = byteToInt(entry, 28, 32);
    createTime.DDay = extract(*CrtDate, 0, 4);
    createTime.DMonth = extract(*CrtDate,5,8);
    createTime.DYear = extract(*CrtDate,9,15) + 1980;
    createTime.DHour = extract(*CrtTime, 11, 15);
    createTime.DMinute = extract(*CrtTime, 5,10);
    createTime.DSecond = extract(*CrtTime,0,4);
    writeTime.DDay = extract(*WrtDate, 0, 4);
    writeTime.DMonth = extract(*WrtDate,5,8);
    writeTime.DYear = extract(*WrtDate,9,15) + 1980;
    writeTime.DHour = extract(*WrtTime, 11, 15);
    writeTime.DMinute = extract(*WrtTime, 5,10);
    writeTime.DSecond = extract(*WrtTime,0,4);
    lastAccessed.DYear = extract(*LstAccDate, 9,15) + 1980;
    lastAccessed.DMonth = extract(*LstAccDate, 5,8);
    lastAccessed.DDay = extract(*LstAccDate, 0, 4);
    created = true;
}

RootEntry::RootEntry(){
    char * temp = new char[1];
    *temp = '1';
    Name = temp;
    longName = temp;
    Attr = 15;
    
    
}

RootEntry::RootEntry(char * name, const char * longname, int attr, int nTRes,SVMDateTime crtTime, SVMDateTime lastAccess, SVMDateTime wrtTime, int FstClusHi, int FstClusLo, int size )
{
    Name = name;
    longName = longname;
    Attr = attr;
    NTRes = nTRes;
    createTime = crtTime;
    writeTime = wrtTime;
    lastAccessed = lastAccess;
    FstClusHI = FstClusHi;
    FstClusLO = FstClusLo;
    FileSize = size;
    created = false;
    
}
vector <TCB*> threads;

vector <Mutex*> mutexes;

vector <memPool*> memoryPools;

vector <File*> files;

vector<File*> changes;


uint16_t** FATEnts;

priority_queue <TCB*, vector<TCB*>, Compare > ready;

TCB * running;
BPB * myBPB;
RootEntry*** rootSectors;

TVMMainEntry VMLoadModule(const char *module);
uint8_t* readSector(int fileDescriptor, int sectorNum, int fileptr);
void scheduler(int code, TCB * thread);
void CB(void* calldat, int result);
void writeSector(int fileDescriptor, int sectorNum, void *data, int fileptr);
uint8_t* readCluster(int fileDescriptor, int cluster, int fileptr);
void writeCluster(int fileDescriptor, int cluster, void* data, int fileptr);
void writeEntry(int fileDescriptor, int sectorNum, void*data, int fileptr);
void writeFAT(int fileDescriptor, int sectorNum, void*data, int fileptr);

void * firstFit(bool * array, int sizeOfArray, int chunk, void * initial, void * object, bool memcopy){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    bool success = true;
    int size = sizeOfArray/64;
    for (int i = 0; i < size; i++)
    {
        if(array[i]){
            if (chunk%64 == 0)
                chunk = (chunk / 64);
            else
                chunk = (chunk / 64)+1;
            for(int j = i; j < chunk+i; j++)
            {
                if (!array[j])
                {
                    success = false;
                    i = j;
                    break;
                }
            }
            if (success)
            {
                if (memcopy)
                    memcpy((char*)initial+(i*64), object, 64*chunk);
                for(int j = i; j < chunk+i; j++)
                {
                    array[j] = false;
                }
                MachineResumeSignals(&sigState);
                return initial+(i*64);
            }
                
            else
                success = true;
        }
    }
    MachineResumeSignals(&sigState);
    return NULL;
}

bool freeByteArray(bool * array, void * initial, void * ptr, int length)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    int check= (char *)ptr - (char *)initial;
    check = check / 64;
    if (array[check] == true)
    {
        MachineResumeSignals(&sigState);
        return false;
    }
    else{
        length = (length / 64);
        for (int i = ((char *)ptr - (char *)initial)/64; i < length+check; i++)
            array[i] = true;
    }
    MachineResumeSignals(&sigState);
    return true;
}

void idleFun(void *param)
{
    MachineEnableSignals();
    TMachineSignalState sigState;
    MachineResumeSignals(&sigState);
    while(1) 
        MachineResumeSignals(&sigState);
    
}

int determineLength(uint8_t * bytes)
{
    for (int i = 0; i < 1024; i++)
    {
        if ((int) bytes[i] == 0)
            return i;
    }
    return 1024;
}


int FirstSectorOfCluster(int N){
    return ((N-2)*myBPB->SecPerClus)+FirstDataSector; 
}

int nextSectorOfCluster(int currentCluster){
    return FirstSectorOfCluster((int) *FATEnts[currentCluster]);
}


int findNextAvaliableCluster(){
    int foundCluster = 0;
    for (int k = 0; k < CountOfClusters+2; k++)
    {
        if (FATEnts[k]){
            if (*FATEnts[k] == 0)
            {
                foundCluster = k;
                uint16_t * temp = new uint16_t;
                *temp = 65535;
                FATEnts[k] = temp;
                break;
            }
        }
        else
            foundCluster = -1;
    }
    return foundCluster;
}

int ThisFATSecNum(int N){
    int FATOffSet = N*2;
    return myBPB->RsvdSecCnt+(FATOffSet/myBPB->BytsPerSec);
}

int ThisFATEntOffset(int N){
    int FATOffSet = N*2;
    return FATOffSet % myBPB->BytsPerSec;
}

RootEntry** getEntriesFromSector(uint8_t *sector){
    RootEntry** entries; 
    entries = new RootEntry*[16];
    
    for(int i = 0; i < 512; i += 32){
        
        
        int attribute = (int) *byteToChar(sector+i, 11, 11+1);
        if (attribute == 15) //if its 15 then its a long name
        {
            while(attribute == 15)
            {
                char * temp = new char[256];
                temp = strcat(temp, unicodeToASCII(sector+i,1,11));
                temp = strcat(temp, unicodeToASCII(sector+i,14,26));
                temp = strcat(temp, unicodeToASCII(sector+i,28,32));
                RootEntry* entry = new RootEntry();
                entries[i/32] = entry;
                i += 32;
                attribute = (int) *byteToChar(sector+i, 11, 11+1);
                longname = strcat(temp, longname);
                if(i>=512){
                    return entries;
                }
            }
        }
        RootEntry* entry = new RootEntry(sector + i, longname);
        entries[i/32] = entry;
        longname = new char[256];
    }
    return entries; 
}

BPB::BPB(uint8_t *buffer){
    BytsPerSec = byteToInt(buffer, 11, 11+2);
    SecPerClus = byteToInt(buffer, 13,14);
    RsvdSecCnt = byteToInt(buffer, 14,14+2);
    NumFATs =  byteToInt(buffer, 16,16+1);
    RootEntCnt =  byteToInt(buffer, 17,17+2);
    TotSec16 =  byteToInt(buffer, 19,19+2);
    Media =  byteToInt(buffer, 21,21+1);
    FATSz16 =  byteToInt(buffer, 22,22+2);
    SecPerTrk = byteToInt(buffer, 24,24+2);
    NumHeads =  byteToInt(buffer, 26,26+2);
    HiddSec =  byteToInt(buffer, 28,28+4);
    TotSec32 =  byteToInt(buffer, 32,32+4);
    DrvNum =  byteToInt(buffer, 36,36+1);
    Reserved1 =  byteToInt(buffer, 37,37+1);
    BootSig =  byteToInt(buffer, 38,38+1);
    VolID =  byteToInt(buffer, 39,39+4);
    
}

File::File(int fileDescriptor, const char* name, int flag, int mode, string location, int cluster, int sector, int entry, RootEntry* corrEntry, bool isdirectory, RootEntry*** Entries){
    FD = fileDescriptor;
    fileName = name;
    flags = flag;
    modes = mode;
    fileLocation = location;
    clusterNO = cluster;
    sectorNO = sector;
    entryNO = entry;
    fileptr = 0;
    isDirectory = isdirectory;
    entries = Entries;
    correspondingEntry = corrEntry;
}
File::File(int fileDescriptor){
    FD = fileDescriptor;
}


void mainThreadCreate(){                                      //creates main and idle threads and system memory pool
    TCB * Main;
    TCB * idle;
    SMachineContext idlecontext;
    idle = new TCB();
    idle->entry = idleFun;
    idle->priority = 0;
    idle->state = VM_THREAD_STATE_READY;
    idle->context = idlecontext;
    idle->memSize = 0x10000;
    //idle->stack = malloc(idle->memSize);
    VMMemoryPoolAllocate(0,idle->memSize, (void**)&idle->stack);
    idle->ID = 1;
    MachineContextCreate(&idle->context,idle->entry, idle, 
            NULL,NULL);
    Main = new TCB();
    running = Main; 
    threads.push_back(Main);
    threads.push_back(idle);
    ready.push(idle);
    MachineFileOpen(image,O_RDWR,0600, CB, (void*)running);
    scheduler(6, running);
    FATfd = running->result;
    uint8_t *theBPB = readSector(FATfd, 0,0);
    myBPB = new BPB(theBPB);
    RootDirSectors = ((myBPB->RootEntCnt*32)+(myBPB->BytsPerSec-1))/myBPB->BytsPerSec;
    FirstDataSector = myBPB->RsvdSecCnt+(myBPB->NumFATs*myBPB->FATSz16)+RootDirSectors;
    DataSec = myBPB->TotSec32-(myBPB->RsvdSecCnt+(myBPB->NumFATs*myBPB->FATSz16)+RootDirSectors);
    CountOfClusters = DataSec/myBPB->SecPerClus; 
    FirstRootSector= myBPB->RsvdSecCnt + myBPB->NumFATs * myBPB->FATSz16;
    rootSectors = new RootEntry**[RootDirSectors];
    for (int i = FirstRootSector; i < RootDirSectors+FirstRootSector; i++) //populating root entries
    {
        uint8_t *sector = readSector(FATfd, i,0);
        rootSectors[i-FirstRootSector] = getEntriesFromSector(sector);
    }
    FATEnts = new uint16_t*[CountOfClusters+2];
 
    for(int i = 0; i < (CountOfClusters+2) / 256; i++){ // populating FAT entries
        uint8_t *FATs = readSector(FATfd, i+1,0);
        for (int j = 0; j < 512; j+= 2)
        {
            FATEnts[(j/2)+(i*256)] = byteToTwoBytes(FATs, j+(i*512),0); 
        }
    }
    if ((CountOfClusters+2)%256 != 0 ) //need to round up
    {
        uint8_t *FATs = readSector(FATfd, ((CountOfClusters+2)/256)+1,0);
        for (int i = ((CountOfClusters+2)/256)*256; i < CountOfClusters+2; i++)
        {
            FATEnts[i] = byteToTwoBytes(FATs, i,0);
        }
    }
    File * in = new File(0);
    File * out = new File(1);
    File* err = new File(2);
    files.push_back(in);
    files.push_back(out);
    files.push_back(err);
    VMMutexCreate(&readwriteMutex);
    mainCreated = true; 
}

void wrapper(void * param){
    MachineEnableSignals();
    running->entry(param);
    VMThreadTerminate(running->ID);
}

bool queueHelper(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (ready.empty())
        return false;
    if(ready.top()->priority >= running->priority)
        return true;
    else
        return false;
}

bool queueHelper2(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (ready.empty())
        return false;
    if(ready.top()->priority > running->priority)
        return true;
    else
        return false;
}

void scheduler(int code, TCB * thread)
{  
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB * temp = running;
    if (code == 6) //running blocked, run next ready
    {
        running->state = VM_THREAD_STATE_WAITING;
        running = ready.top();
        running->state = VM_THREAD_STATE_RUNNING;
        ready.pop();
        //cout << "Thread ID " << running->ID << " is now running!" << endl;
        MachineContextSwitch(&temp->context,&running->context);
        MachineResumeSignals(&sigState);
    }
    else if (code == 1) //Waiting to ready
    {
        if(queueHelper()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
    else if (code == 7) //waiting to ready for mutex
    {
        if(queueHelper2()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }   
    }
    else if (code == 3 ) //quantum is up, running goes to ready.
    {
        if(queueHelper()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            ready.top()->state = VM_THREAD_STATE_RUNNING;
            running = ready.top();
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
    else if(code == 4){
        thread->state = VM_THREAD_STATE_DEAD;
        running = ready.top();
        ready.pop();
        //cout << "Thread ID " << running->ID << " is now running!" << endl;
        //MachineResumeSignals(&sigState);
        MachineContextSwitch(&temp->context, &running->context); 
        MachineResumeSignals(&sigState);
    }
    else if(code == 5){
        thread->state = VM_THREAD_STATE_READY;
        ready.push(thread);
        if(queueHelper2()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
}

memPool::memPool(){
    initialptr = NULL;
    size = 0;
    ID = 0;
}

memPool::memPool(void * init, TVMMemorySize siz, TVMMemoryPoolID eyedee)
{
    initialptr = init;
    size = siz;
    ID = eyedee;
    boolArray = new bool[size/64];
    for(int i = 0; i< size/64; i++){
        boolArray[i] = true;
    }
}

TCB::TCB(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    state = VM_THREAD_STATE_RUNNING;
    MachineContextSave(&context);
    priority = VM_THREAD_PRIORITY_NORMAL;
    //tick = 99999999999999999999999999999999999999999;
    tick = 0;
    sleeping = false;
    result = 0;
    ID = 0;
    MachineResumeSignals(&sigState);
}

TCB::TCB(SMachineContext mContext, TVMThreadID id, TVMThreadPriority prio,
        TVMThreadState stat, TVMMemorySize mSize,TVMThreadEntry mEntry, void * para){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    ID = id;
    priority = prio;
    state = stat;
    memSize = mSize;
    //stack = malloc(mSize);
    VMMemoryPoolAllocate(0,mSize, (void**)&stack);
    context = mContext;
    entry = mEntry;
    //tick = 99999999999999999999999999999999999999999;
    tick = 0;
    sleeping = false;
    result = 0;
    param = para;
    MachineResumeSignals(&sigState);
}

Mutex::Mutex(TVMMutexID id)
{
    locked = false;
    ID = id;
}


void sleepCB(void* callbackTCB){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    tickCounter++;
    for (unsigned i = 0; i < threads.size(); i++)
    {
        if (threads[i]->sleeping && tickCounter >= threads[i]->tick)
        {
            threads[i]->sleeping = false;
            //threads[i]->tick = 99999999999999999999;
            threads[i]->tick = 0;
            threads[i]->state = VM_THREAD_STATE_READY;
            ready.push(threads[i]);
        }

    }
    scheduler(3,running);
    MachineResumeSignals(&sigState);
}

int findNextFreeEntry(uint8_t * sector, int times){
    int prev = 0;
    bool found = false;
        for (int j = prev; j< 16; j++)
        {
            if ((int) sector[j*32] == 0)
            {
                prev = j;
                found = true;
                break;
            }
        }
    if (!found)
        return -1;
    return prev*32;
}

uint8_t* makeByteShortEntry(RootEntry * entry)
{
    uint8_t * byteEntry = new uint8_t[32];
    
    memcpy(byteEntry, entry->Name, 11);
    memcpy(byteEntry+11, intToByte(entry->Attr,1), 1);
    memcpy(byteEntry+12, intToByte(entry->NTRes,1), 1);
    memcpy(byteEntry+13, intToByte(entry->CrtTimeTenth,1), 1);
    uint16_t * temp = new uint16_t;
    uint16_t * CrtTime = new uint16_t;
    *CrtTime = *CrtTime | entry->createTime.DSecond;
    *CrtTime = *CrtTime | entry->createTime.DMinute << 5;
    *CrtTime = *CrtTime | entry->createTime.DHour << 11;
    memcpy(byteEntry+14, CrtTime,2 );
    uint16_t * CrtDate = new uint16_t;
    *CrtDate = *CrtDate | entry->createTime.DDay;
    *CrtDate = *CrtDate | entry->createTime.DMonth << 5;
    *CrtDate = *CrtDate | (entry->createTime.DYear-1980) << 9;
    memcpy(byteEntry+16, CrtDate,2 );
    uint16_t * laDate = new uint16_t;
    *laDate = *laDate | entry->lastAccessed.DDay;
    *laDate = *laDate | entry->lastAccessed.DMonth << 5;
    *laDate = *laDate | (entry->lastAccessed.DYear-1980) << 9;
    memcpy(byteEntry+18, laDate, 2);
    uint16_t * WrtTime = new uint16_t;
    *WrtTime = 0;
    *WrtTime = *WrtTime | (entry->writeTime.DSecond>>1);
    *WrtTime = *WrtTime | (((uint16_t)entry->writeTime.DMinute) << 5);
    *WrtTime = *WrtTime | (((uint16_t)entry->writeTime.DHour) << 11);
    *temp = extract(*WrtTime, 0, 4);
    *temp = extract(*WrtTime, 5, 10);
    *temp = extract(*WrtTime, 11, 15);
    memcpy(byteEntry+22, WrtTime,2);
    uint16_t * WrtDate = new uint16_t;
    *WrtDate = 0;
    *WrtDate = *WrtDate | entry->writeTime.DDay;
    *WrtDate = *WrtDate | (((uint16_t)entry->writeTime.DMonth) << 5);
    *WrtDate = *WrtDate | ((entry->writeTime.DYear-1980) << 9);;
    memcpy(byteEntry+24, WrtDate,2 );
    memcpy(byteEntry+20, intToByte(entry->FstClusHI,2), 2);
    memcpy(byteEntry+26, intToByte(entry->FstClusLO,2), 2);
    memcpy(byteEntry+28, intToByte(entry->FileSize,4), 4);
    return byteEntry;
}

unsigned char ChkSum(char * pFcbName){
    short FcbNameLen;
    unsigned char Sum;
    
    Sum = 0;
    for (FcbNameLen=11; FcbNameLen != 0; FcbNameLen--){
        Sum = ((Sum &1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return Sum;
}

uint8_t* makeByteLongEntry(RootEntry * entry, char * fragment, int stacksize, int i){
    uint8_t * byteEntry = new uint8_t[32];
    bool makeName = true;
    int namepointer = 0;
    if (stacksize == 1 || (stacksize - i) > 1)
        memcpy(byteEntry, intToByte(0x40|(stacksize - i), 1), 1);
    else
        memcpy(byteEntry, intToByte(1,1), 1);
    if (makeName)
        for (int i = 1; i < 10; i+=2){
            memcpy(byteEntry+i, fragment+namepointer, 1);
            namepointer++;
            if (strlen(fragment)<namepointer)
            {
                makeName = false;
                break;
            }
        }
    memcpy(byteEntry+11, intToByte(15,1), 1);
    if (entry->Attr & 0x10 != 0)
        memcpy(byteEntry+12, intToByte(1,1), 1);
    else
        memcpy(byteEntry+12, intToByte(0,1), 1);
    uint8_t * ptr = new uint8_t;
    *ptr = ChkSum(entry->Name);
    memcpy(byteEntry+13, ptr, 1);
    if (makeName)
        for (int i = 14; i < 26; i+=2){
            memcpy(byteEntry+i, fragment+namepointer, 1);
            namepointer++;
            if (strlen(fragment)<namepointer)
            {
                makeName = false;
                break;
            }
        }
    memcpy(byteEntry+26, intToByte(entry->FstClusLO, 2),2);
    if (makeName)
        for (int i = 28; i < 32; i+=2){
            memcpy(byteEntry+i, fragment+namepointer, 1);
            namepointer++;
            if (strlen(fragment)<namepointer)
            {
                makeName = false;
                break;
            }
        }
    return byteEntry;
}

stack<char*> generateLNOrder(const char* longName){
    stack<char *> theStack;
    for (unsigned i = 0; i < strlen(longName); i+= 13){
        char * fragment = new char[13];
        if (strlen(longName)-i < 13 ){
            strncpy(fragment, longName+i, strlen(longName)-i);
            fragment[strlen(longName)-i] = '\0';
            theStack.push(fragment);
        }
        else{
            strncpy(fragment, longName+i, 13 );
            fragment[strlen(longName)] = '\0';
            theStack.push(fragment);
        }
    }
    return theStack;
}



void writeChanges(){
    uint8_t * temp = new uint8_t[512];
    
    for (unsigned i =0; i < changes.size(); i++)
    {
        File* change = changes[i];
        stack <char *>  LNOrder = generateLNOrder(change->correspondingEntry->longName);
        if (!change->fileLocation.compare("/")){
            int stackSize = LNOrder.size();
            int sectorOffset = 0;
            int times = 0;
            if (!change->correspondingEntry->created){
            for (int i =0; i < stackSize; i++){
                int firstFree = findNextFreeEntry(readSector(FATfd,FirstRootSector+change->sectorNO+sectorOffset,0), times);
                if (firstFree != -1){
                    writeEntry(FATfd, FirstRootSector+change->sectorNO+sectorOffset, makeByteLongEntry(change->correspondingEntry, LNOrder.top(), stackSize,i),firstFree);
                    times++;
                }
                else{
                    sectorOffset++;
                    times = 0;
                    firstFree = findNextFreeEntry(readSector(FATfd,FirstRootSector+change->sectorNO+sectorOffset,0), times);
                    writeEntry(FATfd, FirstRootSector+change->sectorNO+sectorOffset, makeByteLongEntry(change->correspondingEntry, LNOrder.top(), stackSize,i),firstFree);
                    times++;
                }
                LNOrder.pop();
            }
            int firstFree = findNextFreeEntry(readSector(FATfd,FirstRootSector+change->sectorNO+sectorOffset,0),times);
            writeEntry(FATfd, FirstRootSector+change->sectorNO+sectorOffset, makeByteShortEntry(change->correspondingEntry),firstFree);      
            }
            else{
                writeEntry(FATfd, FirstRootSector+change->sectorNO+sectorOffset, makeByteShortEntry(change->correspondingEntry),change->entryNO*32);
            }
        }
    }
    for(int i = 0; i < (CountOfClusters+2) / 256; i++){
        uint8_t * bytes = twoBytesToByte(FATEnts, i*512);
        writeSector(FATfd, i+1,bytes,0);
        temp = readSector(FATfd,i+1,0);
        int five = 5;
    }
    //writeFAT(FATfd,1, FATEnts,0);
}

TVMStatus VMStart(int tickms, TVMMemorySize heapsize, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[]){
    byteArrayStart = MachineInitialize(sharedsize);
    sharedSize = sharedsize;
    heapSize = heapsize;
    memPool * systempool;
    uint8_t * base;
    base = new uint8_t[heapSize];
    systempool = new memPool((void *)base, heapSize, 0);
    memPool *sharedMemory;
    sharedMemory = new memPool(byteArrayStart, sharedSize, 1);
    memoryPools.push_back(systempool);
    memoryPools.push_back(sharedMemory);
    MachineRequestAlarm(tickms*1000, sleepCB, NULL);
    tickConversion = tickms;
    image = mount;
    TVMMainEntry main = VMLoadModule(argv[0]);
    main(argc, argv);
    writeChanges();
    TVMMainEntry VMUnLoadModule();
    MachineTerminate();

    return VM_STATUS_SUCCESS;
}

TVMStatus VMTickMS(int *tickmsref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(!tickmsref)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    *tickmsref = tickConversion;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMTickCount(TVMTickRef tickref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(!tickref)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    *tickref = tickCounter;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, 
        TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if(!tid || !entry)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    SMachineContext tempcontext;
    TCB * temp;
    *tid = threadIDCounter;
    temp = new TCB(tempcontext,*tid,prio,VM_THREAD_STATE_DEAD,memsize, entry, param);
    threads.push_back(temp);
    threadIDCounter++;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}  

TCB* findThread(TVMThreadID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < threads.size(); i++){
        if(threads[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return threads[i];
        }
    }
    return NULL;
}

TVMStatus VMThreadDelete(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(foundTCB->state != VM_THREAD_STATE_DEAD){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else 
    {
        vector<TCB *>::iterator pos = find(threads.begin(),threads.end(), foundTCB);
        VMMemoryPoolDeallocate(0,foundTCB->stack);
        threads.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;  
}

TVMStatus VMThreadActivate(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if(foundTCB->state == VM_THREAD_STATE_DEAD){
        MachineContextCreate(&foundTCB->context,wrapper, foundTCB->param, 
                foundTCB->stack,foundTCB->memSize);
        //foundTCB->state = VM_THREAD_STATE_READY;
        //ready.push(foundTCB);
        scheduler(5,foundTCB);
    }
    else
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadTerminate(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if(foundTCB->state == VM_THREAD_STATE_DEAD){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    scheduler(4, foundTCB);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;  
}

TVMStatus VMThreadID(TVMThreadIDRef threadref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!threadref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    else
        *threadref = running->ID;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!stateref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    *stateref = foundTCB->state;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadSleep(TVMTick tick){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(tick == VM_TIMEOUT_INFINITE)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(tick == VM_TIMEOUT_IMMEDIATE)
    {
        MachineResumeSignals(&sigState);
        scheduler(3,running);
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    if (running)
    {
        running->tick = tick+tickCounter;
        running->sleeping = true;
    }
    scheduler(6,running);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

memPool* findMemPool(TVMMemoryPoolID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < memoryPools.size(); i++){
        if(memoryPools[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return memoryPools[i];
        }
    }
    return NULL;
}

TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    if(!base || !memory || size == 0)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    memPool * temp;
    *memory = memPoolIDCounter;
    temp = new memPool(base, size, *memory);
    memoryPools.push_back(temp);
    memPoolIDCounter++;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
    
}

TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    else 
    {
        for (int i = 0; i < foundMemPool->size; i += 64)
        {
            if (!foundMemPool->boolArray[i/64])
            {
                MachineResumeSignals(&sigState);
                return VM_STATUS_ERROR_INVALID_STATE;
            }
        }
        vector<memPool *>::iterator pos = find(memoryPools.begin(),memoryPools.end(), foundMemPool);
        memoryPools.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool || !bytesleft)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    *bytesleft = 0;
    for (int i = 0; i < foundMemPool->size; i += 64)
    {
        if (foundMemPool->boolArray[i/64])
            *bytesleft = *bytesleft + 64;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(!pointer || size == 0)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    *pointer = firstFit(foundMemPool->boolArray,foundMemPool->size, size,foundMemPool->initialptr, *pointer, 0);
    if (!*pointer)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    else
    {
        foundMemPool->ptrSizes[*pointer] = size;
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(!pointer)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    
    if (freeByteArray(foundMemPool->boolArray, foundMemPool->initialptr, pointer, foundMemPool->ptrSizes[pointer]))
    {
        foundMemPool->ptrSizes.erase(pointer);
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    else
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    
}

TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mutexref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    Mutex * temp;
    *mutexref = mutexIDCounter;
    temp = new Mutex(*mutexref);
    mutexIDCounter++;
    mutexes.push_back(temp);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

Mutex* findMutex(TVMMutexID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < mutexes.size(); i++){
        if(mutexes[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return mutexes[i];
        }
    }
    return NULL;
}

TVMStatus VMMutexDelete(TVMMutexID mutex){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(foundMutex->locked)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else 
    {
        vector<Mutex *>::iterator pos = find(mutexes.begin(),mutexes.end(), foundMutex);
        mutexes.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(!ownerref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if(!foundMutex->locked)
    {
        *ownerref = VM_THREAD_ID_INVALID;
    }
    else
    {
        ownerref = &foundMutex->owner;
    }
    
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
    
}

TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if (!foundMutex->locked)
    {
        running->mutex = foundMutex;
        foundMutex->owner = running->ID;
        foundMutex->ownerTCB = running;
        foundMutex->locked = true;
    }
    else if (timeout == VM_TIMEOUT_INFINITE)
    {
        foundMutex->waiting.push(running);
//        while (foundMutex->ownerTCB != running)
//        {
//            running->tick = tickCounter + 1;
//            running->sleeping = true;
            scheduler(6, running);
        //}
        running->mutex = foundMutex;
        foundMutex->owner = running->ID;
        foundMutex->ownerTCB = running;
        
    }
    else if (timeout == VM_TIMEOUT_IMMEDIATE)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    else
    {
        running->tick = tickCounter + timeout;
        foundMutex->waiting.push(running);
        running->sleeping = true;
        scheduler(6, running);
        if (foundMutex->ownerTCB == running)
        {
            running->mutex = foundMutex;
            foundMutex->owner = running->ID;
            foundMutex->ownerTCB = running;
        }
        else
        {
            MachineResumeSignals(&sigState);
            return VM_STATUS_FAILURE;
        }
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexRelease(TVMMutexID mutex){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if (!foundMutex->locked)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else
    {
        foundMutex->ownerTCB->mutex = NULL;
        if (!foundMutex->waiting.empty())
        {
            foundMutex->ownerTCB = foundMutex->waiting.top();
            foundMutex->owner = foundMutex->ownerTCB->ID;
            foundMutex->waiting.pop();
            foundMutex->ownerTCB->state = VM_THREAD_STATE_READY;
            ready.push(foundMutex->ownerTCB);
            scheduler(7,foundMutex->ownerTCB);
        }
        else
            foundMutex->locked = false;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

void CB(void* calldat, int result)
{   
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB * temp2 = (TCB * ) calldat;
    temp2->result = result;
    temp2->state = VM_THREAD_STATE_READY;
    ready.push(temp2);
    scheduler(1, (TCB*) calldat);
    MachineResumeSignals(&sigState);
    
}

TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    if (!filename || !filedescriptor)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    
    int result = -1;
    int foundCluster = 0;
    bool done = false;
    //MachineFileOpen(filename,flags, mode, CB, (void*)running);
    //scheduler(6, running);
    //result = running->result;
    //*filedescriptor = result;
    if(cwd == "/"){
        for(int i = 0; i < RootDirSectors; i++){
            for(int j = 0; j<16; j++){
                if(!strcmp(rootSectors[i][j]->longName, filename)){
                    File *file = new File(fileDescriptorCount, filename, flags, mode, cwd, rootSectors[i][j]->FstClusLO,i,j,rootSectors[i][j],0, NULL);
                    result = fileDescriptorCount;
                    files[fileDescriptorCount] = file;
                    changes.push_back(file);
                    fileDescriptorCount++;
                    done = true;
                    break;
                }
                else
                    result = -1;
            }
            if (done)
                break;
        }
        if(result == -1 && ((flags & O_CREAT)!= 0)){
            done = false;
            for(int i = 0; i < RootDirSectors; i++){
                for(int j = 0; j<16; j++){
                    if(*rootSectors[i][j]->Name == 0){
                        for (int k = 0; k < CountOfClusters+2; k++)
                        {
                            if ( FATEnts[k]){
                                if (*FATEnts[k] == 0)
                                {
                                    foundCluster = k;
                                    uint16_t * temp = new uint16_t;
                                    *temp = 65535;
                                    FATEnts[k] = temp;
                                    break;
                                }
                            }
                            else
                                foundCluster = 0;

                        }
                        if (foundCluster == 0)
                            return VM_STATUS_FAILURE;
                        File *file = new File(fileDescriptorCount, filename, flags, mode, cwd,foundCluster,i,j,NULL,0, NULL);
                        result = fileDescriptorCount;
                        files[fileDescriptorCount] = file;
                        changes.push_back(file);
                        fileDescriptorCount++;
                        char * shortname = calculateShortName(filename);
                        SVMDateTimeRef createTime,lastAccess,writeTime;
                        createTime = new SVMDateTime;
                        lastAccess = new SVMDateTime;
                        writeTime = new SVMDateTime;
                        VMDateTime(createTime);
                        VMDateTime(lastAccess);
                        VMDateTime(writeTime);
                        RootEntry * entry = new RootEntry(shortname, filename, 
                                flags, 0,*createTime, 
                                *lastAccess, *writeTime, 
                                0, foundCluster, 0);
                        rootSectors[i][j] = entry;
                        file->correspondingEntry = entry;
                        done = true;
                        break;
                    }//if match found
                }//for j
                if (done)
                    break;
            }//for i
        }
        
    }
    
    MachineResumeSignals(&sigState);
    if (result < 0)
       return VM_STATUS_FAILURE;
    else
    {
        *filedescriptor = result;
        return VM_STATUS_SUCCESS;
    }
    
}

TVMStatus VMFileClose(int filedescriptor){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    int result = 0;
    
//    MachineFileClose(filedescriptor, CB, (void *)running);
//    scheduler(6, running);
//    result = running->result;
    File * erasefile = new File(filedescriptor);
    files[filedescriptor] = erasefile;
    MachineResumeSignals(&sigState);
    if (result < 0)
       return VM_STATUS_FAILURE;
    else 
       return VM_STATUS_SUCCESS;
    
}

TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if(!data || !length)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    int result = 0;
    int cluster = 0;
    if(filedescriptor < 3){
        if (*length < 512)
        {
            void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,*length, memoryPools[1]->initialptr, data,0);
            MachineFileRead(filedescriptor, ptr, *length, CB, (void*) running);
            scheduler(6, running);
            memcpy(data, ptr, *length);
            freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, *length);
            result = running->result;
            *length = result;
        }
        else
        {
            int  next512 = ((*length / 512)+1)*512;
            void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,next512, memoryPools[1]->initialptr, data,0);
            int theLength = *length;
            for (int i = 0; i < *length; i += 512)
            {  
                if (theLength > 512){
                    MachineFileRead(filedescriptor, ptr, 512, CB, (void*) running);
                    scheduler(6, running);
                    memcpy(data, ptr, 512);
                    result = running->result;
                    theLength -= 512;
                }
                else
                {
                    MachineFileRead(filedescriptor, ptr, theLength, CB, (void*) running);
                    scheduler(6, running);
                    memcpy(data, ptr, theLength);
                    result = running->result;
                }
            }
            freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, next512);
        }
    }
    else{
        File * theFile = files[filedescriptor];
        cluster = theFile->clusterNO;
        if (*length <= 1024)
        {
            int localfileptr = theFile->fileptr;
            int offset = 0;
            while (localfileptr >= 1024)
            {
                offset++;
                localfileptr -= 1024;
            }
            uint8_t * theCluster = readCluster(FATfd, cluster+offset, localfileptr);
            if (*length >= determineLength(theCluster))
                *length = determineLength(theCluster);
            memcpy(data, theCluster, *length);
            theFile->fileptr += *length;
        }
        else{
            int thelength = *length;
            int offset = 0;
            *length = 0;
            int localfileptr = theFile->fileptr;
            for (int i = 0; i < thelength; i+=1024)
            {
                while(localfileptr >= 1024)
                {
                    offset++;
                    localfileptr -= 1024;
                }
                uint8_t * theCluster = readCluster(FATfd, cluster+(i/1024)+offset, localfileptr);
                //*length = strlen((char*) data)-theFile->fileptr-1;
                *length += determineLength(theCluster);
                memcpy((char*)data+i, theCluster, 1024);
                theFile->fileptr += *length;
            }
        }
        SVMDateTimeRef laTime;
        laTime = new SVMDateTime;
        VMDateTime(laTime);
        theFile->correspondingEntry->lastAccessed = *laTime;
    }
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;
        
    
}

TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    if (!data || !length)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    int result = 0;
    int cluster = 0;
    //cout << "Thread ID " << running->ID << " called VMFileWrite!" << endl;
    if(filedescriptor<3){
        if (*length < 512)
        {
            void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,*length, memoryPools[1]->initialptr, data,1);
            MachineFileWrite(filedescriptor,ptr, *length, CB, (void*) running);
            scheduler(6, running);
            freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, *length);
            result = running->result;
            *length = result;
        }
        else
        {
            int next512 = ((*length / 512)+1)*512;
            void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,next512, memoryPools[1]->initialptr, data,1);
            int theLength = *length;
            for (int i = 0; i < *length; i += 512)
            {
                if (theLength > 512){
                    MachineFileWrite(filedescriptor,ptr+i, 512, CB, (void*) running);
                    scheduler(6, running);
                    result = running->result;
                    theLength -= 512;
                }
                else{
                    MachineFileWrite(filedescriptor,ptr+i, theLength, CB, (void*) running);
                    scheduler(6, running);
                    result = running->result;
                }
                
            }
            freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, next512);
        }
    }
    else{
        File * theFile = files[filedescriptor];
        cluster = theFile->clusterNO;
        if (*length <= 1024)
        {
            int localfileptr = theFile->fileptr;
            int offset = 0;
            while (localfileptr >= 1024)
            {
                offset++;
                localfileptr -= 1024;
                if ((int) *FATEnts[cluster] == 65535){
                    int nextCluster = findNextAvaliableCluster();
                    FATEnts[cluster] = (uint16_t*)intToByte(nextCluster,2);
                    FATEnts[nextCluster] = (uint16_t*) intToByte(65535,2); 
                }
                cluster = (int) *FATEnts[cluster];
            }
            writeCluster(FATfd,cluster,data, localfileptr);
            if (1024-localfileptr < *length)
                *length = 1024-localfileptr;
            theFile->fileptr += *length;
            //*length -= localfileptr;
            
        }
        else
        {
            int localfileptr = theFile->fileptr;
            int offset = 0;
            for (int i = 0; i < *length; i+=1024)
            {
                while(localfileptr >= 1024)
                {
                    offset++;
                    localfileptr -= 1024;
                    if ((int) *FATEnts[cluster+(i/1024)] == 65535){
                        int nextCluster = findNextAvaliableCluster();
                        FATEnts[cluster] = (uint16_t*)intToByte(nextCluster,2);
                        FATEnts[nextCluster] = (uint16_t*) intToByte(65535,2);
                    }
                    cluster = (int) *FATEnts[cluster];
                }
                writeCluster(FATfd,cluster+(i/1024),data+i,localfileptr);
                //*length -= localfileptr;
                theFile->fileptr += *length;
            }
        }
        SVMDateTimeRef writeTime;
        writeTime = new SVMDateTime;
        VMDateTime(writeTime);
        theFile->correspondingEntry->writeTime = *writeTime;
        theFile->correspondingEntry->FileSize = theFile->fileptr;
    }
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);    
    int result = 0;
    if (filedescriptor < 3)
    {
        MachineFileSeek(filedescriptor, offset, whence, CB, (void*) running);
        scheduler(6, running);
        result = running->result;
        *newoffset = result;
    }
    else{
        File * theFile = files[filedescriptor];
        theFile->fileptr = offset;
        *newoffset = offset;
    }
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;    
}

TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor){
    TMachineSignalState sigState;
    if (!dirname || !dirdescriptor)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    //Find the dirname in cwd (or it is cwd).  If can't find, return error.
    //turn relative path into absolute path
    //find directory entry using the absolute path
    //generate its ***rootEntry
    //Make a new file object
    if(!strcmp(cwd.c_str(), "/")){
        if(!strcmp(dirname,cwd.c_str())){
            File * rootDir = new File(fileDescriptorCount,cwd.c_str(),0,0,cwd,-1,-1,-1,NULL,1, rootSectors);
            files[fileDescriptorCount] = rootDir;
            *dirdescriptor = fileDescriptorCount;
            fileDescriptorCount++;
        }
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryClose(int dirdescriptor){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    int result = 0;
    File * erasefile = new File(dirdescriptor);
    files[dirdescriptor] = erasefile;
    MachineResumeSignals(&sigState);
    if (result < 0)
       return VM_STATUS_FAILURE;
    else 
       return VM_STATUS_SUCCESS;
    
}

TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(!dirent){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    File * dir = files[dirdescriptor];
    int sector = 0;
    int entry = dir->fileptr;
    while (entry >= 16)
    {
        sector++;
        entry -= 16;
    }
    while (dir->entries[sector][entry]->Attr == 15)
    {
        entry++;
        if (entry >= 16){
            sector++;
            entry -= 16;
        }
    }
    if (!strcmp(dir->entries[sector][entry]->Name, ""))
        return VM_STATUS_FAILURE;
    dir->fileptr = entry + 16*sector;
    strncpy(dirent->DLongFileName, dir->entries[sector][entry]->longName, sizeof(dirent->DLongFileName));
    strncpy(dirent->DShortFileName, dir->entries[sector][entry]->Name, sizeof(dirent->DShortFileName));
    dirent->DAccess = dir->entries[sector][entry]->lastAccessed; 
    dirent->DCreate = dir->entries[sector][entry]->createTime;
    dirent->DAttributes= dir->entries[sector][entry]->Attr;
    dirent->DModify = dir->entries[sector][entry]->writeTime;
    //dirent->DLongFileName= dir->entries[sector][entry]->longName;
    //dirent->DShortFileName = dir->entries[sector][entry]->Name;
    dirent->DSize = dir->entries[sector][entry]->FileSize;
    dir->fileptr++;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryRewind(int dirdescriptor){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (dirdescriptor > fileDescriptorCount || !files[dirdescriptor]->fileName){
        MachineResumeSignals(&sigState);
        return VM_STATUS_FAILURE;
    }
    files[dirdescriptor]->fileptr = 0;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryCurrent(char *abspath){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!abspath){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    strcpy(abspath,cwd.c_str());
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryChange(const char *path){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!path){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (VM_STATUS_SUCCESS == VMFileSystemValidPathName(path))
    {
        if (VM_STATUS_SUCCESS == VMFileSystemPathIsOnMount(image,path)){
            string temp(path);
            cwd = temp;
        }
    }
    return VM_STATUS_SUCCESS;
}

uint8_t* readSector(int fileDescriptor, int sectorNum, int fileptr){ //TODO: add mutex for mutual exclusion of seek+read.
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    VMMutexAcquire(readwriteMutex, VM_TIMEOUT_INFINITE);
    uint8_t * buffer = new uint8_t [512];
    MachineFileSeek(fileDescriptor, sectorNum*512 + fileptr, 0, CB, (void*)running);
    scheduler(6, running);
    void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,512, memoryPools[1]->initialptr, buffer,0);
    MachineFileRead(fileDescriptor, ptr, 512-fileptr, CB, (void*)running);
    scheduler(6, running);
    memcpy(buffer, ptr, 512);
    freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, 512);
    MachineResumeSignals(&sigState);
    VMMutexRelease(readwriteMutex);
    return buffer;    
}

uint8_t* readCluster(int fileDescriptor, int cluster, int fileptr){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    int firstSector = FirstSectorOfCluster(cluster);
    uint8_t* theCluster = new uint8_t[1024];
    if (fileptr < 512)
    {
        memcpy(theCluster, readSector(fileDescriptor,firstSector, fileptr), 512);
        memcpy(theCluster+512, readSector(fileDescriptor,firstSector+1, 0),512);
    }
    else
    {
//        memcpy(theCluster, readSector(fileDescriptor,firstSector, 0), 512);
//        memcpy(theCluster+512, readSector(fileDescriptor,firstSector+1, fileptr),512);
        memcpy(theCluster, readSector(fileDescriptor,firstSector+1, fileptr-512),512);
    }
    MachineResumeSignals(&sigState);
    return theCluster;
    
}

void writeSector(int fileDescriptor, int sectorNum, void *data, int fileptr){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    VMMutexAcquire(readwriteMutex, VM_TIMEOUT_INFINITE);
    MachineFileSeek(fileDescriptor, sectorNum*512+fileptr, 0, CB, (void*)running);
    scheduler(6, running);
    void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,512, memoryPools[1]->initialptr, data,1);
    MachineFileWrite(fileDescriptor,(char*)ptr, 512-fileptr, CB, (void*) running);
    scheduler(6, running);
    freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, 512);
    VMMutexRelease(readwriteMutex);
    MachineResumeSignals(&sigState);
}

void writeCluster(int fileDescriptor, int cluster, void* data, int fileptr){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    int firstSector = FirstSectorOfCluster(cluster);
        
    if (fileptr < 512)
    {
        writeSector(fileDescriptor, firstSector, data, fileptr);
        writeSector(fileDescriptor, firstSector+1, (uint8_t*) data+512, 0);
    }
    else{
//        writeSector(fileDescriptor, firstSector, data, 0);
//        writeSector(fileDescriptor, firstSector+1, (uint8_t*) data+512, fileptr);
        writeSector(fileDescriptor, firstSector+1, (uint8_t*) data, fileptr-512);
        
    }
    MachineResumeSignals(&sigState);
}

void writeEntry(int fileDescriptor, int sectorNum, void*data, int fileptr){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    VMMutexAcquire(readwriteMutex, VM_TIMEOUT_INFINITE);
    MachineFileSeek(fileDescriptor, sectorNum*512+fileptr, 0, CB, (void*)running);
    scheduler(6, running);
    void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,32, memoryPools[1]->initialptr, data,1);
    MachineFileWrite(fileDescriptor,(char*)ptr, 32, CB, (void*) running);
    scheduler(6, running);
    freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, 32);
    VMMutexRelease(readwriteMutex);
    MachineResumeSignals(&sigState);
}

void writeFAT(int fileDescriptor, int sectorNum, void*data, int fileptr){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    VMMutexAcquire(readwriteMutex, VM_TIMEOUT_INFINITE);
    MachineFileSeek(fileDescriptor, sectorNum*512+fileptr, 0, CB, (void*)running);
    scheduler(6, running);
    void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,(CountOfClusters+2)*2, memoryPools[1]->initialptr, data,1);
    MachineFileWrite(fileDescriptor,(char*)ptr, (CountOfClusters+2)*2, CB, (void*) running);
    scheduler(6, running);
    freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, (CountOfClusters+2)*2);
    VMMutexRelease(readwriteMutex);
    MachineResumeSignals(&sigState);
}


} // END OF EXTERN "C" 