#include "mod_archive.h"
#include "file_read.h"
#include "mod_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- inflate --
 * A compact canonical-Huffman decoder after RFC 1951 (structure as in zlib's
 * puff.c). Output never grows past the caller's buffer. */
typedef struct {
    const uint8_t *in;size_t in_size,in_pos;
    uint32_t bit_buffer;unsigned bit_count;
    uint8_t *out;size_t out_size,out_pos;
} Inflate;
typedef struct { uint16_t count[16],symbol[288]; } Huffman;

static int bits(Inflate *s,unsigned need,unsigned *value)
{
    uint32_t buffer=s->bit_buffer;
    while(s->bit_count<need){
        if(s->in_pos>=s->in_size)return 0;
        buffer|=(uint32_t)s->in[s->in_pos++]<<s->bit_count;
        s->bit_count+=8u;
    }
    *value=(unsigned)(buffer&((1u<<need)-1u));
    s->bit_buffer=buffer>>need;s->bit_count-=need;
    return 1;
}

static int build(Huffman *h,const uint8_t *lengths,unsigned n)
{
    uint16_t offsets[16];
    memset(h->count,0,sizeof h->count);
    for(unsigned i=0u;i<n;++i)h->count[lengths[i]]++;
    if(h->count[0]==n)return 1; /* no codes: only valid if never used */
    int left=1;
    for(unsigned len=1u;len<16u;++len){
        left<<=1;left-=h->count[len];
        if(left<0)return 0; /* over-subscribed */
    }
    offsets[1]=0u;
    for(unsigned len=1u;len<15u;++len)offsets[len+1u]=(uint16_t)(offsets[len]+h->count[len]);
    for(unsigned i=0u;i<n;++i)if(lengths[i]!=0u)h->symbol[offsets[lengths[i]]++]=(uint16_t)i;
    return 1;
}

static int decode(Inflate *s,const Huffman *h)
{
    int code=0,first=0,index=0;
    for(unsigned len=1u;len<16u;++len){
        unsigned bit;
        if(!bits(s,1u,&bit))return -1;
        code|=(int)bit;
        const int count=h->count[len];
        if(code-count<first)return h->symbol[index+(code-first)];
        index+=count;first+=count;first<<=1;code<<=1;
    }
    return -1;
}

static int codes(Inflate *s,const Huffman *lengths,const Huffman *distances)
{
    static const uint16_t length_base[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t length_extra[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t distance_base[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t distance_extra[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for(;;){
        int symbol=decode(s,lengths);
        if(symbol<0)return 0;
        if(symbol<256){
            if(s->out_pos>=s->out_size)return 0;
            s->out[s->out_pos++]=(uint8_t)symbol;
        }else if(symbol==256)return 1;
        else{
            symbol-=257;
            if(symbol>=29)return 0;
            unsigned extra;
            if(!bits(s,length_extra[symbol],&extra))return 0;
            const size_t length=length_base[symbol]+extra;
            symbol=decode(s,distances);
            if(symbol<0||symbol>=30)return 0;
            if(!bits(s,distance_extra[symbol],&extra))return 0;
            const size_t distance=distance_base[symbol]+extra;
            if(distance>s->out_pos||length>s->out_size-s->out_pos)return 0;
            for(size_t i=0u;i<length;++i,++s->out_pos)s->out[s->out_pos]=s->out[s->out_pos-distance];
        }
    }
}

static int stored(Inflate *s)
{
    s->bit_buffer=0u;s->bit_count=0u;
    if(s->in_size-s->in_pos<4u)return 0;
    const unsigned length=s->in[s->in_pos]|(unsigned)s->in[s->in_pos+1u]<<8;
    const unsigned complement=s->in[s->in_pos+2u]|(unsigned)s->in[s->in_pos+3u]<<8;
    s->in_pos+=4u;
    if(length!=(~complement&0xffffu)||length>s->in_size-s->in_pos||length>s->out_size-s->out_pos)return 0;
    if(length!=0u)memcpy(s->out+s->out_pos,s->in+s->in_pos,length);
    s->out_pos+=length;s->in_pos+=length;
    return 1;
}

static int fixed(Inflate *s)
{
    Huffman lengths,distances;
    uint8_t table[288];
    unsigned i=0u;
    for(;i<144u;++i)table[i]=8u;
    for(;i<256u;++i)table[i]=9u;
    for(;i<280u;++i)table[i]=7u;
    for(;i<288u;++i)table[i]=8u;
    build(&lengths,table,288u);
    memset(table,5,30u);
    build(&distances,table,30u);
    return codes(s,&lengths,&distances);
}

static int dynamic(Inflate *s)
{
    static const uint8_t order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    uint8_t table[320]={0};
    Huffman lengths,distances;
    unsigned literal_count,distance_count,code_count;
    if(!bits(s,5u,&literal_count)||!bits(s,5u,&distance_count)||!bits(s,4u,&code_count))return 0;
    literal_count+=257u;distance_count+=1u;code_count+=4u;
    if(literal_count>286u||distance_count>30u)return 0;
    for(unsigned i=0u;i<code_count;++i){unsigned value;if(!bits(s,3u,&value))return 0;table[order[i]]=(uint8_t)value;}
    if(!build(&lengths,table,19u))return 0;
    memset(table,0,sizeof table);
    for(unsigned index=0u;index<literal_count+distance_count;){
        int symbol=decode(s,&lengths);
        if(symbol<0)return 0;
        if(symbol<16){table[index++]=(uint8_t)symbol;continue;}
        unsigned repeat,value=0u;
        if(symbol==16){
            if(index==0u||!bits(s,2u,&repeat))return 0;
            value=table[index-1u];repeat+=3u;
        }else if(symbol==17){if(!bits(s,3u,&repeat))return 0;repeat+=3u;}
        else{if(!bits(s,7u,&repeat))return 0;repeat+=11u;}
        if(index+repeat>literal_count+distance_count)return 0;
        while(repeat--)table[index++]=(uint8_t)value;
    }
    if(table[256]==0u)return 0;
    if(!build(&lengths,table,literal_count)||!build(&distances,table+literal_count,distance_count))return 0;
    return codes(s,&lengths,&distances);
}

int portable_inflate(const uint8_t *input,size_t input_size,uint8_t *output,size_t output_size)
{
    if(input==NULL||(output==NULL&&output_size!=0u))return 0;
    Inflate s={input,input_size,0u,0u,0u,output,output_size,0u};
    unsigned last;
    do{
        unsigned type;
        if(!bits(&s,1u,&last)||!bits(&s,2u,&type))return 0;
        const int ok=type==0u?stored(&s):type==1u?fixed(&s):type==2u?dynamic(&s):0;
        if(!ok)return 0;
    }while(!last);
    return s.out_pos==output_size;
}

uint32_t portable_crc32(const uint8_t *bytes,size_t size)
{
    uint32_t crc=UINT32_C(0xffffffff);
    for(size_t i=0u;i<size;++i){
        crc^=bytes[i];
        for(unsigned bit=0u;bit<8u;++bit)crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}

/* ---------------------------------------------------------------- archive -- */
typedef struct {
    char name[256];
    uint32_t crc,compressed,uncompressed,local_offset;
    size_t data_offset;
    uint16_t method;
} ArchiveEntry;
struct PortableModArchive {
    uint8_t *bytes;size_t size;
    ArchiveEntry *entries;size_t count;
    char prefix[80],root[80];
};

static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

static int fail(char *error,const char *reason)
{
    if(error!=NULL)snprintf(error,PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY,"%s",reason);
    return 0;
}

static int name_valid(const char *name)
{
    if(name[0]=='\0'||name[0]=='/')return 0;
    const char *segment=name;
    for(const char *c=name;;++c){
        if(*c=='\\'||*c==':'||((unsigned char)*c<32u&&*c!='\0')||(unsigned char)*c>126u)return 0;
        if(*c=='/'||*c=='\0'){
            const size_t length=(size_t)(c-segment);
            if((length==0u&&*c!='\0')||(length==1u&&segment[0]=='.')||(length==2u&&segment[0]=='.'&&segment[1]=='.'))return 0;
            if(*c=='\0')return 1;
            segment=c+1;
        }
    }
}

static const ArchiveEntry *find(const PortableModArchive *archive,const char *name)
{
    for(size_t i=0u;i<archive->count;++i)if(strcmp(archive->entries[i].name,name)==0)return &archive->entries[i];
    return NULL;
}

static int parse(PortableModArchive *archive,char *error)
{
    const uint8_t *bytes=archive->bytes;
    const size_t size=archive->size;
    if(size<22u)return fail(error,"not a zip archive");
    size_t end=size-22u;
    const size_t lowest=size>22u+65535u?size-22u-65535u:0u;
    for(;;){
        if(le32(bytes+end)==UINT32_C(0x06054b50)&&end+22u+le16(bytes+end+20u)==size)break;
        if(end==lowest)return fail(error,"no zip end-of-directory record");
        --end;
    }
    const uint8_t *eocd=bytes+end;
    const uint32_t count=le16(eocd+10u),directory_size=le32(eocd+12u),directory_offset=le32(eocd+16u);
    if(le16(eocd+4u)!=0u||le16(eocd+6u)!=0u||le16(eocd+8u)!=count)return fail(error,"multi-disk zip archives are not supported");
    if(count==0u||count>PORTABLE_MOD_ARCHIVE_MAX_ENTRIES)return fail(error,"zip holds no entries or more than 256");
    if(directory_offset>end||directory_size>end-directory_offset)return fail(error,"zip central directory out of range");
    archive->entries=calloc(count,sizeof *archive->entries);
    if(archive->entries==NULL)return fail(error,"out of memory");
    uint64_t total=0u;
    size_t at=directory_offset;
    for(uint32_t i=0u;i<count;++i){
        if(at+46u>directory_offset+(size_t)directory_size||le32(bytes+at)!=UINT32_C(0x02014b50))return fail(error,"corrupt zip central directory");
        const uint8_t *h=bytes+at;
        const uint16_t flags=le16(h+8u),method=le16(h+10u),name_length=le16(h+28u);
        const size_t record=46u+name_length+le16(h+30u)+le16(h+32u);
        if(at+record>directory_offset+(size_t)directory_size)return fail(error,"corrupt zip central directory");
        if(flags&1u)return fail(error,"encrypted zip entries are not supported");
        if(method!=0u&&method!=8u)return fail(error,"zip entries must be stored or deflated");
        if(name_length==0u||name_length>=sizeof archive->entries[0].name)return fail(error,"zip entry name empty or too long");
        ArchiveEntry *entry=&archive->entries[archive->count];
        if(memchr(h+46u,0,name_length)!=NULL)return fail(error,"zip entry name contains NUL");
        memcpy(entry->name,h+46u,name_length);entry->name[name_length]='\0';
        entry->method=method;entry->crc=le32(h+16u);entry->compressed=le32(h+20u);
        entry->uncompressed=le32(h+24u);entry->local_offset=le32(h+42u);
        at+=record;
        const int folder=entry->name[name_length-1u]=='/';
        if(folder)entry->name[name_length-1u]='\0';
        char reason[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
        if(!name_valid(entry->name)){
            snprintf(reason,sizeof reason,"zip entry %.120s: unsafe path",entry->name);return fail(error,reason);
        }
        if(folder)entry->name[name_length-1u]='/';
        if(entry->compressed==UINT32_C(0xffffffff)||entry->uncompressed==UINT32_C(0xffffffff))return fail(error,"zip64 archives are not supported");
        if(entry->uncompressed>PORTABLE_MOD_ARCHIVE_MAX_ENTRY_BYTES||
           (method==0u&&entry->compressed!=entry->uncompressed)){
            snprintf(reason,sizeof reason,"zip entry %.120s: size exceeds 64 MiB or is inconsistent",entry->name);return fail(error,reason);
        }
        total+=entry->uncompressed;
        if(total>PORTABLE_MOD_ARCHIVE_MAX_TOTAL_BYTES)return fail(error,"zip expands past the 320 MiB total cap");
        if(find(archive,entry->name)!=NULL){
            snprintf(reason,sizeof reason,"zip entry %.120s: duplicate name",entry->name);return fail(error,reason);
        }
        /* Local header and data must lie inside the archive before the directory. */
        const size_t local=entry->local_offset;
        if(local>directory_offset||directory_offset-local<30u||
           le32(bytes+local)!=UINT32_C(0x04034b50)||
           le16(bytes+local+26u)!=name_length||le16(bytes+local+8u)!=method||
           le16(bytes+local+6u)!=flags){
            snprintf(reason,sizeof reason,"zip entry %.120s: local header mismatch",entry->name);return fail(error,reason);
        }
        const size_t header_size=30u+name_length+le16(bytes+local+28u);
        if(header_size>directory_offset-local){
            snprintf(reason,sizeof reason,"zip entry %.120s: local header out of range",entry->name);return fail(error,reason);
        }
        if(memcmp(bytes+local+30u,h+46u,name_length)!=0){
            snprintf(reason,sizeof reason,"zip entry %.120s: local header mismatch",entry->name);return fail(error,reason);
        }
        const size_t data=local+header_size;
        if(entry->compressed>directory_offset-data){
            snprintf(reason,sizeof reason,"zip entry %.120s: data out of range",entry->name);return fail(error,reason);
        }
        entry->data_offset=data;
        ++archive->count;
    }
    return 1;
}

PortableModArchive *portable_mod_archive_open_memory(uint8_t *bytes,size_t size,
    const char *root_folder,char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY])
{
    if(error!=NULL)error[0]='\0';
    PortableModArchive *archive=bytes!=NULL?calloc(1u,sizeof *archive):NULL;
    if(archive==NULL){free(bytes);fail(error,"out of memory");return NULL;}
    archive->bytes=bytes;archive->size=size;
    if(size>PORTABLE_MOD_ARCHIVE_MAX_BYTES){fail(error,"zip exceeds 96 MiB");portable_mod_archive_free(archive);return NULL;}
    if(!parse(archive,error)){portable_mod_archive_free(archive);return NULL;}
    if(find(archive,"mod.json")==NULL){
        if(root_folder!=NULL&&strlen(root_folder)<sizeof archive->prefix-10u){
            char name[96];snprintf(name,sizeof name,"%s/mod.json",root_folder);
            if(find(archive,name)!=NULL){
                snprintf(archive->prefix,sizeof archive->prefix,"%s/",root_folder);
                snprintf(archive->root,sizeof archive->root,"%s",root_folder);
            }
        }else if(root_folder==NULL){
            for(size_t i=0u;i<archive->count;++i){
                const char *name=archive->entries[i].name,*slash=strchr(name,'/');
                if(slash==NULL||strcmp(slash+1u,"mod.json")!=0)continue;
                const size_t length=(size_t)(slash-name);
                char id[PORTABLE_MOD_ID_CAPACITY];
                if(length>=sizeof id){fail(error,"nested manifest folder is not a valid mod ID");goto invalid_root;}
                memcpy(id,name,length);id[length]='\0';
                if(!portable_mod_id_valid(id)){fail(error,"nested manifest folder is not a valid mod ID");goto invalid_root;}
                if(archive->prefix[0]!='\0'){fail(error,"multiple nested mod manifests");goto invalid_root;}
                snprintf(archive->prefix,sizeof archive->prefix,"%s/",id);
                snprintf(archive->root,sizeof archive->root,"%s",id);
            }
            if(archive->prefix[0]=='\0'){fail(error,"no root or one-level mod manifest");goto invalid_root;}
        }
    }
    return archive;
invalid_root:
    portable_mod_archive_free(archive);return NULL;
}

PortableModArchive *portable_mod_archive_open(const char *path,const char *root_folder,
    char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY])
{
    if(error!=NULL)error[0]='\0';
    uint8_t *bytes=NULL;
    size_t size=0u;
    const PortableFileReadStatus status=portable_file_read_bounded(path,
        PORTABLE_MOD_ARCHIVE_MAX_BYTES,&bytes,&size);
    if(status!=PORTABLE_FILE_READ_OK){
        fail(error,status==PORTABLE_FILE_READ_LIMIT?"zip exceeds 96 MiB":"zip unreadable");
        return NULL;
    }
    if(size==0u){free(bytes);fail(error,"zip unreadable or empty");return NULL;}
    return portable_mod_archive_open_memory(bytes,size,root_folder,error);
}

/* Parsing proves data_offset/compressed bounds once. Reads and installation
 * validation then use the same exact-size decoder and CRC contract. */
static uint8_t *decode_entry(const PortableModArchive *archive,const ArchiveEntry *entry,char *error)
{
    const uint8_t *data=archive->bytes+entry->data_offset;
    uint8_t *out=malloc(entry->uncompressed!=0u?entry->uncompressed:1u);
    if(out==NULL){fail(error,"out of memory");return NULL;}
    int ok;
    if(entry->method==0u){
        if(entry->uncompressed!=0u)memcpy(out,data,entry->uncompressed);
        ok=1;
    }else ok=portable_inflate(data,entry->compressed,out,entry->uncompressed);
    if(!ok){free(out);fail(error,"corrupt deflate data or size mismatch");return NULL;}
    if(portable_crc32(out,entry->uncompressed)!=entry->crc){free(out);fail(error,"CRC mismatch");return NULL;}
    return out;
}

int portable_mod_archive_validate(const PortableModArchive *archive,
    char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY])
{
    if(error!=NULL)error[0]='\0';
    if(archive==NULL)return fail(error,"invalid argument");
    for(size_t i=0u;i<archive->count;++i){
        char reason[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY];
        uint8_t *bytes=decode_entry(archive,&archive->entries[i],reason);
        if(bytes==NULL){
            if(error!=NULL)snprintf(error,PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY,
                "zip entry %.100s: %.70s",archive->entries[i].name,reason);
            return 0;
        }
        free(bytes);
    }
    return 1;
}

const uint8_t *portable_mod_archive_bytes(const PortableModArchive *archive,size_t *size)
{
    if(size!=NULL)*size=archive!=NULL?archive->size:0u;
    return archive!=NULL?archive->bytes:NULL;
}

const char *portable_mod_archive_root(const PortableModArchive *archive)
{return archive!=NULL?archive->root:NULL;}

uint8_t *portable_mod_archive_read(const PortableModArchive *archive,const char *name,
    size_t maximum,size_t *size,char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY])
{
    if(error!=NULL)error[0]='\0';
    if(archive==NULL||name==NULL||size==NULL){fail(error,"invalid argument");return NULL;}
    char full[256];
    const int length=snprintf(full,sizeof full,"%s%s",archive->prefix,name);
    const ArchiveEntry *entry=length>0&&(size_t)length<sizeof full?find(archive,full):NULL;
    if(entry==NULL){fail(error,"missing from zip");return NULL;}
    if(entry->uncompressed==0u||entry->uncompressed>maximum){fail(error,"empty or over its size limit");return NULL;}
    uint8_t *out=decode_entry(archive,entry,error);
    if(out==NULL)return NULL;
    *size=entry->uncompressed;
    return out;
}

void portable_mod_archive_free(PortableModArchive *archive)
{
    if(archive==NULL)return;
    free(archive->entries);free(archive->bytes);free(archive);
}
