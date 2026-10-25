/* SPDX-License-Identifier: GPL-2.0-only */
/* MSI B650M/AD103 ownership adapter. Snapshot/delta strategy adapted from
 * impacifist/Windows-GPU-UNLOCK 9352abd internal-boot/boot/prepare-console.c.
 * Driver identities/code were captured in CAP0003. No driver code/data patch.
 * No firmware variables, controller reconnects or reset fallback are used.
 */
#include "msi_release.h"
#include "boot_io.h"
#include "profile.h"
#include "sha256.h"
#include "compat/compat.h"
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <IndustryStandard/Acpi10.h>

#define MAX_EDGES 4096
typedef struct {EFI_HANDLE handle,agent,controller;EFI_GUID protocol;UINT32 attributes,count;} EDGE;
typedef struct {UINTN count;EDGE edges[MAX_EDGES];} GRAPH;
typedef struct {
  EFI_SYSTEM_TABLE *table;EFI_HANDLE self,pci,gop,sibling,out_agent,gfx_agent,nv_agent;
  EFI_HANDLE console,input,error,pointer_agent,key_agent,usb_host,usb_agent;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text,*err;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *keys;
  UINTN key_reset,key_read;EFI_EVENT key_wait;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop_interface;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
  UINT32 config[64];UINT64 attributes;
  UINTN stage,reported_additions,reported_removals;BOOLEAN ready,mutated,claimed;
} PLAN;
static PLAN Plan;
static GRAPH Original,Current;
static struct {const char *operation;EFI_HANDLE handle;EFI_GUID protocol;} SnapshotTrace;
static struct {const char *operation;EFI_HANDLE handle;UINTN roots,gops,siblings;} ChildTrace;
static BOOLEAN OriginalUsb[MAX_EDGES];
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,TextGuid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
static EFI_GUID InputGuid=EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
static EFI_GUID PointerGuid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0,0x90,0x27,0x3f,0xc1,0x4d}};
/* CAP0003 exposes both pointer protocols through the same splitter agent. */
static EFI_GUID AbsolutePointerGuid={0x8d59d32b,0xc655,0x4ae9,{0x9b,0x15,0xf2,0x59,0x04,0x99,0x2a,0x43}};
static EFI_GUID UsbHcGuid={0x3e745226,0x9818,0x45b6,{0xa2,0xac,0xd7,0xcd,0x0e,0x8b,0xa2,0xbc}};
static const UINT8 UsbHostPath[]={0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x01,0x02,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x00,0x0c,0x01,0x01,0x06,0x00,0x00,0x00,0x7f,0xff,0x04,0x00};
static EFI_GUID BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,ImageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const UINT8 PciPath[]={0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x01,0x01,0x01,0x01,0x06,0x00,0x00,0x00,0x7f,0xff,0x04,0x00};
static const UINT8 GopPath[]={0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x01,0x01,0x01,0x01,0x06,0x00,0x00,0x00,0x02,0x03,0x08,0x00,0x40,0x03,0x01,0x80,0x7f,0xff,0x04,0x00};
static const UINT8 SiblingPath[]={0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x01,0x01,0x01,0x01,0x06,0x00,0x00,0x00,0x02,0x03,0x08,0x00,0x70,0x03,0x01,0x80,0x7f,0xff,0x04,0x00};
static const UINT8 OutFile[]={4,6,20,0,0x7d,0x49,0x8a,0x62,0xf6,0x2b,0x64,0x42,0x87,0x41,6,0x9d,0xbd,0x33,0x99,0xd6,127,255,4,0};
static const UINT8 GfxFile[]={4,6,20,0,0xdd,0xab,0xe7,0x43,0x52,0xe3,0xfb,0x4c,0xa2,0x30,0x4c,0xdc,0x1d,0x35,0x0e,0x5c,127,255,4,0};
static const UINT8 NvFile[]={4,4,28,0,'n',0,'v',0,'g',0,'o',0,'p',0,'-',0,'a',0,'d',0,'1',0,'x',0,'x',0,0,0,127,255,4,0};

static BOOLEAN Equal(const VOID *a,const VOID *b,UINTN n){return !memcmp(a,b,n);}
static BOOLEAN Guid(const EFI_GUID *a,const EFI_GUID *b){return Equal(a,b,16);}
static BOOLEAN PointerProtocol(const EFI_GUID *g){return Guid(g,&PointerGuid)||Guid(g,&AbsolutePointerGuid);}
static EFI_STATUS Interface(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  *p=NULL;EFI_STATUS s=Plan.table->BootServices->HandleProtocol(h,g,p);
  return s==EFI_SUCCESS&&!*p?EFI_COMPROMISED_DATA:s;
}
/* Match exact, bounded, single-instance paths; never accept a prefix alone. */
static BOOLEAN Path(EFI_HANDLE h,const UINT8 *bytes,UINTN count) {
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS)return FALSE;
  UINTN at=0;
  while(at+4<=count) {
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4||n>count-at||!Equal(p,bytes+at,n))return FALSE;
    if(p->Type==END_DEVICE_PATH_TYPE)return n==4&&p->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE&&at+4==count;
    at+=n;p=(VOID *)((UINT8 *)p+n);
  }
  return FALSE;
}
static BOOLEAN PciBranch(const EFI_DEVICE_PATH_PROTOCOL *p) {
  const UINT8 *at=(const UINT8 *)p;UINTN offset=0,prefix=sizeof(PciPath)-4;
  while(offset<prefix) {
    UINTN n=at[2]+((UINTN)at[3]<<8);
    UINTN expected=PciPath[offset+2]+((UINTN)PciPath[offset+3]<<8);
    if(n!=expected||n>prefix-offset||!Equal(at,PciPath+offset,n))return FALSE;
    at+=n;offset+=n;
  }
  return TRUE;
}
/* Cross-check all GPU paths, including handles with no active ownership edge.
 * Freeze the inventoried child set until NVIDIA Stop removes the children. */
static EFI_STATUS Children(BOOLEAN first) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *handles=NULL,sibling=NULL;UINTN count=0;
  memset(&ChildTrace,0,sizeof(ChildTrace));ChildTrace.operation="LocateHandleBuffer";
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&PathGuid,NULL,&count,&handles);
  if(s==EFI_SUCCESS&&(!handles||!count||count>4096))s=EFI_BAD_BUFFER_SIZE;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *p=NULL;ChildTrace.handle=handles[i];ChildTrace.operation="HandleProtocol";
    s=Interface(handles[i],&PathGuid,(VOID **)&p);if(s!=EFI_SUCCESS)break;
    if(!PciBranch(p))continue;
    ChildTrace.operation="GPU-path-or-handle";
    if(Path(handles[i],PciPath,sizeof(PciPath))) {
      if(++ChildTrace.roots!=1||handles[i]!=Plan.pci)s=EFI_ACCESS_DENIED;
    } else if(Path(handles[i],GopPath,sizeof(GopPath))) {
      if(++ChildTrace.gops!=1||Plan.stage>=3||handles[i]!=Plan.gop)s=EFI_ACCESS_DENIED;
    } else if(Path(handles[i],SiblingPath,sizeof(SiblingPath))) {
      if(++ChildTrace.siblings!=1||Plan.stage>=3||(!first&&handles[i]!=Plan.sibling))s=EFI_ACCESS_DENIED;
      sibling=handles[i];
    } else s=EFI_ACCESS_DENIED;
  }
  if(s==EFI_SUCCESS) {
    ChildTrace.operation="GPU-path-counts";
    if(ChildTrace.roots!=1||ChildTrace.gops!=(Plan.stage>=3?0u:1u)||
       (!first&&ChildTrace.siblings!=(Plan.stage>=3||!Plan.sibling?0u:1u)))s=EFI_ACCESS_DENIED;
  }
  if(handles&&b->FreePool(handles)!=EFI_SUCCESS){ChildTrace.operation="FreePool";s=EFI_DEVICE_ERROR;}
  if(s==EFI_SUCCESS&&first)Plan.sibling=sibling;
  return s;
}
static EFI_STATUS Snapshot(GRAPH *g) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *handles=NULL;UINTN count=0;g->count=0;
  memset(&SnapshotTrace,0,sizeof(SnapshotTrace));SnapshotTrace.operation="LocateHandleBuffer";
  EFI_STATUS s=b->LocateHandleBuffer(AllHandles,NULL,NULL,&count,&handles);
  if(s==EFI_SUCCESS&&(!handles||!count||count>4096))s=EFI_BAD_BUFFER_SIZE;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GUID **protocols=NULL;UINTN pc=0;
    SnapshotTrace.operation="ProtocolsPerHandle";SnapshotTrace.handle=handles[i];
    memset(&SnapshotTrace.protocol,0,sizeof(SnapshotTrace.protocol));
    s=b->ProtocolsPerHandle(handles[i],&protocols,&pc);
    if(s==EFI_SUCCESS&&(!protocols||!pc||pc>64))s=EFI_BAD_BUFFER_SIZE;
    for(UINTN j=0;s==EFI_SUCCESS&&j<pc;j++) {
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN ec=0;
      SnapshotTrace.operation="OpenProtocolInformation";
      memset(&SnapshotTrace.protocol,0,sizeof(SnapshotTrace.protocol));
      if(!protocols[j]){s=EFI_COMPROMISED_DATA;break;}
      SnapshotTrace.protocol=*protocols[j];
      s=b->OpenProtocolInformation(handles[i],protocols[j],&entries,&ec);
      if(s==EFI_SUCCESS&&(ec>128||(ec&&!entries)))s=EFI_BAD_BUFFER_SIZE;
      for(UINTN k=0;s==EFI_SUCCESS&&k<ec;k++) {
        EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&entries[k];
        if(e->Attributes==1||e->Attributes==2||e->Attributes==4)continue;
        BOOLEAN own=Plan.claimed&&handles[i]==Plan.pci&&e->AgentHandle==Plan.self&&
          !e->ControllerHandle&&e->Attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->OpenCount==1;
        if(g->count==MAX_EDGES||!e->AgentHandle||(!e->ControllerHandle&&!own)||!e->OpenCount){s=EFI_COMPROMISED_DATA;break;}
        g->edges[g->count++]=(EDGE){handles[i],e->AgentHandle,e->ControllerHandle,*protocols[j],e->Attributes,e->OpenCount};
      }
      if(entries&&b->FreePool(entries)!=EFI_SUCCESS){SnapshotTrace.operation="FreePool-entries";s=EFI_DEVICE_ERROR;}
    }
    if(protocols&&b->FreePool(protocols)!=EFI_SUCCESS){SnapshotTrace.operation="FreePool-protocols";s=EFI_DEVICE_ERROR;}
  }
  if(handles&&b->FreePool(handles)!=EFI_SUCCESS){SnapshotTrace.operation="FreePool-handles";s=EFI_DEVICE_ERROR;}
  return s;
}
static BOOLEAN SameEdge(const EDGE *a,const EDGE *b) {
  return a->handle==b->handle&&a->agent==b->agent&&a->controller==b->controller&&
    a->attributes==b->attributes&&a->count==b->count&&Guid(&a->protocol,&b->protocol);
}
static BOOLEAN Removed(const EDGE *e,UINTN stage) {
  if(stage>=1&&e->handle==Plan.gop&&e->agent==Plan.out_agent&&Guid(&e->protocol,&TextGuid))return TRUE;
  if(stage>=2&&e->handle==Plan.gop&&e->agent==Plan.gfx_agent&&Guid(&e->protocol,&GopGuid))return TRUE;
  return stage>=3&&e->handle==Plan.pci&&e->agent==Plan.nv_agent;
}
/* Original unrelated relationships must remain present. New relationships can
 * appear during USB enumeration; they cannot involve any protected endpoint.
 * A protocol GUID alone never authorizes access to the GPU or its owners. */
static BOOLEAN ProtectedHandle(EFI_HANDLE h) {
  return h&&(h==Plan.pci||h==Plan.gop||h==Plan.sibling||h==Plan.self||h==Plan.out_agent||
    h==Plan.gfx_agent||h==Plan.nv_agent||h==Plan.console||h==Plan.input||h==Plan.error);
}
static BOOLEAN ProtectedEdge(const EDGE *e) {
  return ProtectedHandle(e->handle)||ProtectedHandle(e->agent)||ProtectedHandle(e->controller);
}
static BOOLEAN OwnEdge(const EDGE *e) {
  return e->handle==Plan.pci&&e->agent==Plan.self&&!e->controller&&
    Guid(&e->protocol,&PciGuid)&&e->attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->count==1;
}
static BOOLEAN KnownEdge(const EDGE *e,UINTN stage) {
  for(UINTN i=0;i<Original.count;i++)
    if(!Removed(&Original.edges[i],stage)&&SameEdge(&Original.edges[i],e))return TRUE;
  return FALSE;
}
/* A late USB mouse attaches to the existing virtual console (RUN0005).
 * Only the preexisting pointer splitter may add this exact child relationship,
 * and it must also own that physical pointer BY_DRIVER on a non-GPU USB path.
 * No exception is based on a GUID prefix or on a raw handle address alone. */
static BOOLEAN UsbPointerPath(EFI_HANDLE h) {
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS)return FALSE;
  /* Match complete nodes of the captured AMD USB branch. A short unrelated
   * device path must be rejected before reading the next expected node. */
  UINT8 *at=(UINT8 *)p;UINTN offset=0,prefix=sizeof(UsbHostPath)-4;
  while(offset<prefix) {
    UINTN n=at[2]+((UINTN)at[3]<<8);
    UINTN expected=UsbHostPath[offset+2]+((UINTN)UsbHostPath[offset+3]<<8);
    if(n<4||n!=expected||n>prefix-offset||!Equal(at,UsbHostPath+offset,n))return FALSE;
    offset+=n;at+=n;
  }
  for(UINTN depth=0;depth<8;depth++) {
    if(at[0]!=MESSAGING_DEVICE_PATH||at[1]!=MSG_USB_DP||at[2]!=6||at[3]!=0)return FALSE;
    at+=6;
    if(at[0]==END_DEVICE_PATH_TYPE)return at[1]==END_ENTIRE_DEVICE_PATH_SUBTYPE&&at[2]==4&&at[3]==0;
  }
  return FALSE;
}
static BOOLEAN PointerOwner(const GRAPH *g,const EDGE *e) {
  UINTN owners=0;
  for(UINTN i=0;i<g->count;i++) {
    const EDGE *o=&g->edges[i];
    if(o->handle==e->handle&&Guid(&o->protocol,&e->protocol)&&
       o->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER) {
      if(o->agent!=e->agent||o->controller!=e->handle||o->count!=1)return FALSE;
      owners++;
    }
  }
  return owners==1;
}
static BOOLEAN PointerAddition(const EDGE *e) {
  BOOLEAN splitter=(PointerProtocol(&e->protocol)&&Plan.pointer_agent&&e->agent==Plan.pointer_agent)||
    (Guid(&e->protocol,&InputGuid)&&Plan.key_agent&&e->agent==Plan.key_agent);
  return splitter&&e->controller==Plan.console&&
    !ProtectedHandle(e->handle)&&!ProtectedHandle(e->agent)&&
    e->attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER&&e->count==1&&
    PointerOwner(&Current,e)&&UsbPointerPath(e->handle);
}
static EFI_STATUS PointerBaseline(VOID) {
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    BOOLEAN pointer=PointerProtocol(&e->protocol),key=Guid(&e->protocol,&InputGuid);
    if((!pointer&&!key)||e->controller!=Plan.console||e->attributes!=8)continue;
    EFI_HANDLE *agent=pointer?&Plan.pointer_agent:&Plan.key_agent;
    if(e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent)||
       !PointerOwner(&Original,e)||!UsbPointerPath(e->handle)||
       (*agent&&e->agent!=*agent))return EFI_UNSUPPORTED;
    *agent=e->agent;
  }
  return EFI_SUCCESS;
}
/* Freeze which ORIGINAL relationships belong to this board's USB branch
 * while all their handles/paths still exist. Later removal never consults a
 * possibly freed or recycled handle. The USB host's driver/PCI ownership and
 * every GPU/display/application relationship remain mandatory. */
static BOOLEAN UsbConsoleChild(const EDGE *e,const GRAPH *g) {
  BOOLEAN splitter=(PointerProtocol(&e->protocol)&&Plan.pointer_agent&&e->agent==Plan.pointer_agent)||
    (Guid(&e->protocol,&InputGuid)&&Plan.key_agent&&e->agent==Plan.key_agent);
  return splitter&&e->controller==Plan.console&&e->attributes==8&&e->count==1&&PointerOwner(g,e);
}
static EFI_STATUS UsbBaseline(VOID) {
  memset(OriginalUsb,0,sizeof(OriginalUsb));
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(!Guid(&e->protocol,&UsbHcGuid)||e->attributes!=0x10||e->controller!=e->handle||
       e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent)||
       !Path(e->handle,UsbHostPath,sizeof(UsbHostPath)))continue;
    if(Plan.usb_host)return EFI_UNSUPPORTED;
    Plan.usb_host=e->handle;Plan.usb_agent=e->agent;
  }
  if(!Plan.usb_host||!Plan.usb_agent)return EFI_UNSUPPORTED;
  UINTN dynamic=0;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent))continue;
    BOOLEAN child=e->handle==Plan.usb_host&&e->agent==Plan.usb_agent&&
      Guid(&e->protocol,&UsbHcGuid)&&e->attributes==8&&!ProtectedHandle(e->controller)&&
      UsbPointerPath(e->controller);
    BOOLEAN physical=UsbPointerPath(e->handle);
    BOOLEAN device=physical&&((e->attributes==0x10&&e->controller==e->handle)||
      (e->attributes==8&&!ProtectedHandle(e->controller)&&UsbPointerPath(e->controller))||
      UsbConsoleChild(e,&Original));
    if(child||device){OriginalUsb[i]=TRUE;dynamic++;}
  }
  GuLog("MSI USB baseline: host=%#llx bus_agent=%#llx dynamic_edges=%zu; host ownership pinned\n",
    (unsigned long long)(UINTN)Plan.usb_host,(unsigned long long)(UINTN)Plan.usb_agent,(size_t)dynamic);
  return EFI_SUCCESS;
}
static BOOLEAN UsbRemoved(UINTN i) {
  if(!OriginalUsb[i])return FALSE;
  const EDGE *e=&Original.edges[i];
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *c=&Current.edges[j];
    if(c->handle==e->handle&&c->agent==e->agent&&c->controller==e->controller&&Guid(&c->protocol,&e->protocol))return FALSE;
  }
  return TRUE;
}
static BOOLEAN Expected(UINTN stage,BOOLEAN claimed) {
  UINTN own=0;
  for(UINTN i=0;i<Original.count;i++) {
    if(Removed(&Original.edges[i],stage))continue;
    UINTN matches=0;
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(matches!=1&&!(matches==0&&UsbRemoved(i)))return FALSE;
  }
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *e=&Current.edges[j];
    if(OwnEdge(e)){own++;continue;}
    if(!KnownEdge(e,stage)&&ProtectedEdge(e)&&!PointerAddition(e))return FALSE;
    /* Even unrelated new relationships must not be duplicated. */
    for(UINTN k=j+1;k<Current.count;k++)if(SameEdge(e,&Current.edges[k]))return FALSE;
  }
  return own==(claimed?1u:0u);
}
/* v0.5.1 diagnostics: GuLog faults once the USB log is closed, so log only while it is open. */
#define NOTE(...) do{if(GuBootLogOpen())GuLog(__VA_ARGS__);}while(0)
static VOID NoteEdge(const char *kind,const EDGE *e) {
  NOTE("MSI graph %s: handle=%#llx agent=%#llx controller=%#llx protocol=%#x attributes=%#x count=%u\n",kind,
    (unsigned long long)(UINTN)e->handle,(unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)e->controller,
    e->protocol.Data1,e->attributes,e->count);
}
static VOID NoteGuid(const EFI_GUID *g) {
  NOTE("MSI protocol GUID: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
    g->Data1,(unsigned)g->Data2,(unsigned)g->Data3,(unsigned)g->Data4[0],(unsigned)g->Data4[1],
    (unsigned)g->Data4[2],(unsigned)g->Data4[3],(unsigned)g->Data4[4],(unsigned)g->Data4[5],
    (unsigned)g->Data4[6],(unsigned)g->Data4[7]);
}
static EFI_STATUS ConsumerRefused(const char *reason,const EDGE *e) {
  NOTE("MSI consumers refused: %s\n",reason);
  if(e){NoteEdge("refused-consumer",e);NoteGuid(&e->protocol);}
  return EFI_ACCESS_DENIED;
}
/* Name the first stage-3 edge that is missing or unexpected after a failed Expected(). */
static VOID GraphDiff(BOOLEAN claimed) {
  for(UINTN i=0;i<Original.count;i++) {
    if(Removed(&Original.edges[i],3))continue;
    UINTN matches=0;
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(matches!=1&&!(matches==0&&UsbRemoved(i))){NoteEdge(matches?"duplicate":"missing",&Original.edges[i]);return;}
  }
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *e=&Current.edges[j];
    if(!KnownEdge(e,3)&&!(claimed&&OwnEdge(e))&&ProtectedEdge(e)&&!PointerAddition(e)){NoteEdge("unexpected",e);return;}
    for(UINTN k=j+1;k<Current.count;k++)if(SameEdge(e,&Current.edges[k])){NoteEdge("duplicate",e);return;}
  }
}
static VOID ReportAdditions(VOID) {
  UINTN added=0,removed=0;
  for(UINTN i=0;i<Original.count;i++)if(UsbRemoved(i))removed++;
  if(removed!=Plan.reported_removals&&GuBootLogOpen()) {
    NOTE("MSI graph: %zu original USB relationships absent; host/GPU ownership and console callbacks unchanged\n",(size_t)removed);
    Plan.reported_removals=removed;
  }
  for(UINTN j=0;j<Current.count;j++)
    if(!OwnEdge(&Current.edges[j])&&!KnownEdge(&Current.edges[j],3))added++;
  if(added&&added!=Plan.reported_additions&&GuBootLogOpen()) {
    NOTE("MSI graph: accepted %zu new edges (unrelated or verified USB pointer); GPU ownership unchanged\n",(size_t)added);
    for(UINTN j=0,shown=0;j<Current.count&&shown<8;j++)
      if(!OwnEdge(&Current.edges[j])&&!KnownEdge(&Current.edges[j],3)){NoteEdge("new-unrelated",&Current.edges[j]);shown++;}
    Plan.reported_additions=added;
  }
}
static EFI_STATUS Driver(UINTN which) {
  EFI_HANDLE a=which==0?Plan.out_agent:which==1?Plan.gfx_agent:Plan.nv_agent;
  EFI_DRIVER_BINDING_PROTOCOL *d=NULL;EFI_LOADED_IMAGE_PROTOCOL *im=NULL;
  EFI_STATUS s=Interface(a,&BindingGuid,(VOID **)&d);
  if(s!=EFI_SUCCESS||!d->Stop||d->DriverBindingHandle!=a||!d->ImageHandle||
     d->Version!=(which==2?0x70011u:0x10u))return EFI_UNSUPPORTED;
  s=Interface(d->ImageHandle,&ImageGuid,(VOID **)&im);if(s!=EFI_SUCCESS)return s;
  UINTN size=which==0?28160:which==1?18464:162496;
  UINTN stop=which==0?0x1480:which==1?0xe28:0x6ec;
  UINTN code=0x280,bytes=which==0?22968:which==1?10336:138183;
  const char *hash=which==0?"06ee7c7e50fb7c5d5f334b84b138f450f08a5fce46f4f1b25af3ce00e0e1c533":
    which==1?"cfd7655e24fd210074f3952bb1d4b47c1cb0456c4419fc154dbd60878ca4f262":
    "216c7270fa08ad9e4c3694d34d825e9e904554a1af5ffa2aeead4317c5537b37";
  const UINT8 *file=which==0?OutFile:which==1?GfxFile:NvFile;
  UINTN file_bytes=which==2?sizeof(NvFile):sizeof(OutFile);
  if(!im->ImageBase||im->ImageSize!=size||im->ImageCodeType!=EfiBootServicesCode||
     (UINTN)d->Stop!=(UINTN)im->ImageBase+stop||!im->FilePath||
     !Equal(im->FilePath,file,file_bytes)||code+bytes>size||
     (which==2&&(im->DeviceHandle!=Plan.pci||d->ImageHandle!=a)))return EFI_UNSUPPORTED;
  /* CAP0003 found no relocations in these code ranges. Hash loaded code,
   * never relocated data or a driver-global guessed from another BIOS. */
  return GuHashMatches((UINT8 *)im->ImageBase+code,bytes,hash)?EFI_SUCCESS:EFI_CRC_ERROR;
}
static EFI_STATUS Alive(VOID) {
  EFI_SYSTEM_TABLE *t=Plan.table;VOID *in=NULL,*out=NULL,*err=NULL;
  if(t->ConsoleOutHandle!=Plan.console||t->ConsoleInHandle!=Plan.input||t->StandardErrorHandle!=Plan.error||
     t->ConIn!=Plan.keys||t->ConOut!=Plan.text||t->StdErr!=Plan.err||!t->ConIn||
     (UINTN)t->ConIn->Reset!=Plan.key_reset||(UINTN)t->ConIn->ReadKeyStroke!=Plan.key_read||
     t->ConIn->WaitForKey!=Plan.key_wait)return EFI_NOT_READY;
  EFI_STATUS s=Interface(Plan.input,&InputGuid,&in);
  if(s==EFI_SUCCESS)s=Interface(Plan.console,&TextGuid,&out);
  if(s==EFI_SUCCESS)s=Interface(Plan.error,&TextGuid,&err);
  return s==EFI_SUCCESS&&(in!=Plan.keys||out!=Plan.text||err!=Plan.err)?EFI_NOT_READY:s;
}
static EFI_STATUS Gops(BOOLEAN first) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *h=NULL;UINTN count=0,physical=0;
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&h);
  if(Plan.stage>=3&&s==EFI_NOT_FOUND)return EFI_SUCCESS;
  if(s==EFI_SUCCESS&&(count>2||(count&&!h)))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;s=Interface(h[i],&GopGuid,(VOID **)&g);if(s!=EFI_SUCCESS)break;
    if(h[i]==Plan.console&&g->Mode&&!g->Mode->FrameBufferBase&&!g->Mode->FrameBufferSize)continue;
    if(Plan.stage>=3||!Path(h[i],GopPath,sizeof(GopPath))||++physical!=1||!g->Mode||!g->Mode->Info||
       g->Mode->SizeOfInfo<sizeof(*g->Mode->Info)||!g->Mode->FrameBufferBase||!g->Mode->FrameBufferSize){s=EFI_UNSUPPORTED;break;}
    if(first) {Plan.gop=h[i];Plan.gop_interface=g;Plan.mode=*g->Mode;Plan.info=*g->Mode->Info;}
    else if(h[i]!=Plan.gop||g!=Plan.gop_interface||!Equal(g->Mode,&Plan.mode,sizeof(Plan.mode))||!Equal(g->Mode->Info,&Plan.info,sizeof(Plan.info)))s=EFI_NOT_READY;
  }
  if(h&&b->FreePool(h)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS&&physical!=(Plan.stage>=3?0u:1u))s=EFI_NOT_READY;
  return s;
}
static EFI_STATUS ReadConfig(UINT32 c[64]) {
  EFI_PCI_IO_PROTOCOL *io=NULL;UINTN seg,bus,dev,fun;
  EFI_STATUS s=Interface(Plan.pci,&PciGuid,(VOID **)&io);
  if(s!=EFI_SUCCESS||io!=Gu.pci||!io->Pci.Read||!io->GetLocation)return EFI_NOT_READY;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS||seg||bus!=1||dev||fun)return EFI_UNSUPPORTED;
  s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,c);
  if(s==EFI_SUCCESS&&(!GuTargetConfig(c,seg,bus,dev,fun)||(c[1]&4)))s=EFI_UNSUPPORTED;
  return s;
}
/* RUN0002 did not identify PCI drift: do not assume it caused the stop.
 * Retain v0.5.0's narrow rule: only preexisting CED/URD may clear after FLR. */
static BOOLEAN ConfigStable(const UINT32 c[64]) {
  for(UINTN i=0;i<64;i++) {
    UINT32 drift=c[i]^Plan.config[i];
    if(!drift)continue;
    BOOLEAN allowed=Gu.flrs==1&&i==0x80/4&&Plan.config[0x78/4]==0x0012b410&&
      !(c[i]&~Plan.config[i])&&!(drift&~0x00090000u);
    if(!allowed) {
      NOTE("PCI config drift refused: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
        (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
      return FALSE;
    }
    NOTE("PCI status drift tolerated: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
      (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
  }
  return TRUE;
}
static EFI_STATUS Bar(VOID) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=NULL;UINT64 support=0;
  if(!Gu.pci->GetBarAttributes)return EFI_UNSUPPORTED;
  EFI_STATUS s=Gu.pci->GetBarAttributes(Gu.pci,0,&support,(VOID **)&d);
  if(s==EFI_SUCCESS&&(!d||d->Desc!=0x8a||d->Len!=43||d->ResType||
     d->AddrRangeMin!=GU_BAR0_BASE||d->AddrRangeMax!=0xf5ffffff||
     d->AddrTranslationOffset||d->AddrLen!=0x1000000))s=EFI_UNSUPPORTED;
  if(d&&Plan.table->BootServices->FreePool(d)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
static EFI_STATUS Consumers(UINTN *verified) {
  *verified=0;
  UINTN pci=0,child=0,sibling=0,text=0,virtual=0,gfx=0;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle==Plan.pci) {
      if(e->count!=1)return ConsumerRefused("pci-open-count",e);
      if(Guid(&e->protocol,&PciGuid)&&e->attributes==0x10&&e->controller==Plan.pci){Plan.nv_agent=e->agent;pci++;}
      else if(Guid(&e->protocol,&PathGuid)&&e->attributes==8&&e->controller==Plan.gop){child++;}
      else if(Guid(&e->protocol,&PathGuid)&&e->attributes==8&&
              e->controller==Plan.sibling&&Path(e->controller,SiblingPath,sizeof(SiblingPath))) {
        sibling++;
      }
      else return ConsumerRefused("pci-relationship",e);
    } else if(e->handle==Plan.gop) {
      if(e->count!=1)return ConsumerRefused("gop-open-count",e);
      if(Guid(&e->protocol,&TextGuid)&&e->attributes==0x10&&e->controller==Plan.gop){Plan.out_agent=e->agent;text++;}
      else if(Guid(&e->protocol,&TextGuid)&&e->attributes==8&&e->controller==Plan.console){virtual++;}
      else if(Guid(&e->protocol,&GopGuid)&&e->attributes==0x10&&e->controller==Plan.gop){Plan.gfx_agent=e->agent;gfx++;}
      else return ConsumerRefused("gop-relationship",e);
    }
  }
  NOTE("MSI consumer counts: pci_owner=%zu gop_child=%zu sibling_child=%zu text_owner=%zu console_child=%zu gop_owner=%zu\n",
    (size_t)pci,(size_t)child,(size_t)sibling,(size_t)text,(size_t)virtual,(size_t)gfx);
  if(pci!=1||child!=1||sibling!=(Plan.sibling?1u:0u)||Plan.sibling==Plan.gop||
     text!=1||virtual!=1||gfx!=1||Plan.nv_agent==Plan.out_agent||
     Plan.out_agent==Plan.gfx_agent||Plan.gfx_agent==Plan.nv_agent)return ConsumerRefused("required-relationships-or-distinct-agents",NULL);
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle==Plan.pci&&e->agent!=Plan.nv_agent)return ConsumerRefused("pci-agent",e);
    if(e->handle==Plan.gop&&Guid(&e->protocol,&TextGuid)&&e->agent!=Plan.out_agent)return ConsumerRefused("text-agent",e);
    /* A present secondary child requires its sole parent edge and no consumers. */
    if(Plan.sibling&&(e->handle==Plan.sibling||e->agent==Plan.sibling))return ConsumerRefused("sibling-consumer",e);
    if(Plan.sibling&&e->controller==Plan.sibling&&!(e->handle==Plan.pci&&e->agent==Plan.nv_agent&&
       Guid(&e->protocol,&PathGuid)&&e->attributes==8&&e->count==1))return ConsumerRefused("sibling-parent",e);
  }
  for(UINTN i=0;i<3;i++){
    EFI_STATUS s=Driver(i);
    if(s!=EFI_SUCCESS){NOTE("MSI driver refused: index=%zu status=%#llx\n",(size_t)i,(unsigned long long)s);return s;}
    (*verified)++;
  }
  return EFI_SUCCESS;
}
EFI_STATUS GuMsiPreflight(EFI_SYSTEM_TABLE *t,EFI_HANDLE self,EFI_HANDLE pci) {
  if(Plan.mutated||Plan.ready)return EFI_ALREADY_STARTED;
  memset(&Plan,0,sizeof(Plan));Plan.table=t;Plan.self=self;Plan.pci=pci;
  Original.count=0;
  if(!t||!t->BootServices||!self||!pci||Gu.handle!=pci||Gu.image!=self||Gu.bs!=t->BootServices||
     !Gu.profile_verified||!Gu.prom_verified||Gu.started||Gu.owned||Gu.armed)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *b=t->BootServices;
  if(!b->DisconnectController||!b->OpenProtocol||!b->CloseProtocol||!b->RaiseTPL||!b->RestoreTPL||
     !b->ProtocolsPerHandle||!b->OpenProtocolInformation||!b->LocateHandleBuffer||!b->HandleProtocol)return EFI_UNSUPPORTED;
  Plan.console=t->ConsoleOutHandle;Plan.input=t->ConsoleInHandle;Plan.error=t->StandardErrorHandle;
  Plan.text=t->ConOut;Plan.err=t->StdErr;Plan.keys=t->ConIn;
  if(!Plan.console||Plan.console!=Plan.input||Plan.console!=Plan.error||!Plan.text||!Plan.err||!Plan.keys||
     !Plan.keys->ReadKeyStroke||!Plan.keys->Reset||!Plan.keys->WaitForKey)return EFI_UNSUPPORTED;
  Plan.key_reset=(UINTN)Plan.keys->Reset;Plan.key_read=(UINTN)Plan.keys->ReadKeyStroke;Plan.key_wait=Plan.keys->WaitForKey;
  if(!Path(pci,PciPath,sizeof(PciPath))||!GuAd103ColdEntry())return EFI_NOT_READY;
  const char *check="gop";UINTN verified=0;BOOLEAN snapshot_complete=FALSE;
  EFI_STATUS s=Gops(TRUE);
  if(s==EFI_SUCCESS){check="console";s=Alive();}
  if(s==EFI_SUCCESS){check="bar";s=Bar();}
  if(s==EFI_SUCCESS){check="config";s=ReadConfig(Plan.config);}
  if(s==EFI_SUCCESS){check="attributes";s=Gu.pci->Attributes?Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&Plan.attributes):EFI_UNSUPPORTED;}
  UINT64 decode=(Plan.config[1]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(Plan.config[1]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
  if(s==EFI_SUCCESS&&(Plan.attributes&(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER))!=decode)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS){check="snapshot";s=Snapshot(&Original);snapshot_complete=s==EFI_SUCCESS;
    if(s!=EFI_SUCCESS){NOTE("MSI snapshot refused: operation=%s handle=%#llx\n",SnapshotTrace.operation,(unsigned long long)(UINTN)SnapshotTrace.handle);NoteGuid(&SnapshotTrace.protocol);}
  }
  if(s==EFI_SUCCESS){check="child-paths";s=Children(TRUE);
    NOTE("MSI child paths: status=%#llx operation=%s handle=%#llx pci=%zu gop=%zu sibling=%zu\n",
      (unsigned long long)s,ChildTrace.operation,(unsigned long long)(UINTN)ChildTrace.handle,
      (size_t)ChildTrace.roots,(size_t)ChildTrace.gops,(size_t)ChildTrace.siblings);
  }
  if(s==EFI_SUCCESS){check="consumers";s=Consumers(&verified);}
  if(s==EFI_SUCCESS){check="pointer-baseline";s=PointerBaseline();}
  if(s==EFI_SUCCESS){check="usb-baseline";s=UsbBaseline();}
  GuLog("MSI preflight: status=%#llx check=%s graph=%zu snapshot_complete=%u code_profiles=%zu\n",
    (unsigned long long)s,check,(size_t)Original.count,snapshot_complete,(size_t)verified);
  if(s!=EFI_SUCCESS) {
    UINTN shown=0;
    for(UINTN i=0;i<Original.count;i++) {
      const EDGE *e=&Original.edges[i];
      if(e->handle!=Plan.pci&&e->handle!=Plan.gop&&e->handle!=Plan.sibling&&e->controller!=Plan.sibling)continue;
      if(shown++==32){NOTE("MSI preflight GPU graph truncated at 32 relationships\n");break;}
      NoteEdge("preflight",e);NoteGuid(&e->protocol);
    }
  }
  if(s==EFI_SUCCESS)GuLog("MSI endpoints: pci=%#llx gop=%#llx console=%#llx pointer_agent=%#llx\n",
    (unsigned long long)(UINTN)Plan.pci,(unsigned long long)(UINTN)Plan.gop,
    (unsigned long long)(UINTN)Plan.console,(unsigned long long)(UINTN)Plan.pointer_agent);
  if(s==EFI_SUCCESS)Plan.ready=TRUE;return s;
}
static BOOLEAN DecodeMask(const UINT32 c[64],UINT64 *mask) {
  *mask=0;UINT32 missing=Plan.config[1]&~c[1]&3;
  if((Plan.config[1]&6)!=2||(c[1]&4))return FALSE;
  for(UINTN i=0;i<64;i++)if((c[i]|(i==1?missing:0))!=Plan.config[i])return FALSE;
  *mask=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);return TRUE;
}
static EFI_STATUS ClaimRestore(VOID) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_PCI_IO_PROTOCOL *io=NULL;
  UINT32 c[64];UINT64 mask=0,attrs=0,supported=0;
  EFI_STATUS s=ReadConfig(c);if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  GuLog("MSI decode restore intent: status=%#llx IO_MEM_mask=%#llx\n",(unsigned long long)s,(unsigned long long)mask);
  if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
  EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS)s=Children(FALSE);
  if(s==EFI_SUCCESS&&!Expected(3,FALSE))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=b->OpenProtocol(Plan.pci,&PciGuid,(VOID **)&io,Plan.self,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE);
  if(s==EFI_SUCCESS){Plan.claimed=TRUE;Gu.owned=TRUE;if(io!=Gu.pci)s=EFI_COMPROMISED_DATA;}
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=(Plan.attributes&~mask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationSupported,0,&supported);
  if(s==EFI_SUCCESS&&(supported&mask)!=mask)s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS)s=Children(FALSE);
  if(s==EFI_SUCCESS&&!Expected(3,TRUE))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS&&mask)s=io->Attributes(io,EfiPciIoAttributeOperationEnable,mask,NULL);
  b->RestoreTPL(old);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=Plan.attributes)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Bar();
  if(s==EFI_SUCCESS&&!GuAd103ColdEntry())s=EFI_NOT_READY;
  GuLog("MSI exclusive claim/decode result: status=%#llx\n",(unsigned long long)s);return s;
}
EFI_STATUS GuMsiRelease(VOID) {
  if(!Plan.ready||Plan.mutated||Gu.started||Gu.owned)return EFI_NOT_READY;
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;
  for(UINTN stage=1;stage<=3;stage++) {
    GuLog("MSI driver detach intent: stage=%zu\n",(size_t)stage);
    EFI_STATUS s=Alive();UINT32 c[64];
    if(s==EFI_SUCCESS)s=ReadConfig(c);
    if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS&&!GuAd103ColdEntry())s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Gops(FALSE);
    EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
    if(s==EFI_SUCCESS)s=Snapshot(&Current);
    if(s==EFI_SUCCESS)s=Children(FALSE);
    if(s==EFI_SUCCESS&&!Expected(stage-1,FALSE))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Driver(stage-1);
    b->RestoreTPL(old);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    /* Record before the callback: an error may follow a partial driver stop. */
    Plan.mutated=TRUE;
    s=b->DisconnectController(stage==3?Plan.pci:Plan.gop,
      stage==1?Plan.out_agent:stage==2?Plan.gfx_agent:Plan.nv_agent,NULL);
    GuLog("MSI driver detach return: stage=%zu status=%#llx\n",(size_t)stage,(unsigned long long)s);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    Plan.stage=stage;
    s=Snapshot(&Current);
    if(s==EFI_SUCCESS)s=Children(FALSE);
    if(s==EFI_SUCCESS&&!Expected(stage,FALSE))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Alive();
    if(s==EFI_SUCCESS)s=Gops(FALSE);
    if(s!=EFI_SUCCESS)return s;
  }
  EFI_STATUS s=ClaimRestore();
  if(s==EFI_SUCCESS&&!GuMsiReleased())s=EFI_NOT_READY;
  return s;
}
BOOLEAN GuMsiReleased(VOID) {
  UINT32 c[64];UINT64 attrs=0;const char *failed=NULL;
  if(!Plan.ready||Plan.stage!=3||!Plan.claimed||!Gu.owned||Gu.fault)failed="state";
  else if(Alive()!=EFI_SUCCESS)failed="console";
  else if(Gops(FALSE)!=EFI_SUCCESS)failed="GOP";
  else if(Children(FALSE)!=EFI_SUCCESS)failed="child-paths";
  else if(ReadConfig(c)!=EFI_SUCCESS)failed="config-read";
  else if(!ConfigStable(c))failed="config-drift";
  else if(Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attrs)!=EFI_SUCCESS||attrs!=Plan.attributes)failed="attributes";
  else if(Snapshot(&Current)!=EFI_SUCCESS)failed="graph-snapshot";
  else if(!Expected(3,TRUE)){failed="graph";GraphDiff(TRUE);}
  if(failed)NOTE("MSI release check failed: %s attributes=%#llx expected=%#llx graph=%zu original=%zu fault=%u flrs=%u\n",
    failed,(unsigned long long)attrs,(unsigned long long)Plan.attributes,(size_t)Current.count,(size_t)Original.count,Gu.fault,Gu.flrs);
  if(!failed)ReportAdditions();
  return !failed;
}
EFI_STATUS GuMsiFinish(VOID) {
  if(!GuSafeToReturn()||!Gu.quiesced||Gu.flrs!=1||!GuMsiReleased()){NOTE("MSI finish refused before close\n");return EFI_NOT_READY;}
  EFI_STATUS s=Plan.table->BootServices->CloseProtocol(Plan.pci,&PciGuid,Plan.self,NULL);
  if(s!=EFI_SUCCESS){NOTE("MSI finish close status=%#llx\n",(unsigned long long)s);return s;}
  Plan.claimed=FALSE;Gu.owned=FALSE;
  s=Snapshot(&Current);if(s==EFI_SUCCESS&&!Expected(3,FALSE)){s=EFI_NOT_READY;GraphDiff(FALSE);}
  if(s!=EFI_SUCCESS)NOTE("MSI finish after close status=%#llx graph=%zu\n",(unsigned long long)s,(size_t)Current.count);
  return s;
}
BOOLEAN GuMsiMutated(VOID){return Plan.mutated;}
BOOLEAN GuMsiReadyForWindows(VOID) {
  if(!Plan.ready||Plan.stage!=3||Plan.claimed||Gu.owned||Gu.fault||!Gu.quiesced||Gu.flrs!=1||!GuSafeToReturn())return FALSE;
  UINT32 c[64];
  if(Alive()!=EFI_SUCCESS||Gops(FALSE)!=EFI_SUCCESS||Children(FALSE)!=EFI_SUCCESS||ReadConfig(c)!=EFI_SUCCESS||!ConfigStable(c)||
     Snapshot(&Current)!=EFI_SUCCESS||!Expected(3,FALSE))return FALSE;
  /* Fresh status after USB Close and before StartImage; no log callbacks. */
  UINT32 chip=GuRead32(0),cpu=GuRead32(0x110100),hi=GuRead32(0x1fa828);
  UINT32 ss0=GuRead32(0x82381c),ss1=GuRead32(0x823820),readout=GuRead32(0x823818),plm=GuRead32(0x823804);
  return !Gu.fault&&chip==0x193000a1&&(cpu&0x10)&&cpu!=0xffffffff&&
    (cpu&0xffff0000)!=0xbadf0000&&!(hi>>4)&&ss0==0x88888888&&ss1==8&&
    !(readout&0x07ffffff)&&readout!=0xffffffff&&plm==0xffffffff;
}
