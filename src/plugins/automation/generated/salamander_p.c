

/* this ALWAYS GENERATED file contains the proxy stub code */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 04:14:07 2038
 */
/* Compiler settings for ..\salamander.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.01.0628 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if !defined(_M_IA64) && !defined(_M_AMD64) && !defined(_ARM_)


#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/
#pragma warning( disable: 4152 )  /* function/data pointer conversion in expression */
#pragma warning( disable: 4100 ) /* unreferenced arguments in x86 call */

#pragma optimize("", off ) 

#define USE_STUBLESS_PROXY


/* verify that the <rpcproxy.h> version is high enough to compile this file*/
#ifndef __REDQ_RPCPROXY_H_VERSION__
#define __REQUIRED_RPCPROXY_H_VERSION__ 475
#endif


#include "rpcproxy.h"
#ifndef __RPCPROXY_H_VERSION__
#error this stub requires an updated version of <rpcproxy.h>
#endif /* __RPCPROXY_H_VERSION__ */


#include "salamander_h.h"

#define TYPE_FORMAT_STRING_SIZE   1277                              
#define PROC_FORMAT_STRING_SIZE   2887                              
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   2            

typedef struct _salamander_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } salamander_MIDL_TYPE_FORMAT_STRING;

typedef struct _salamander_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } salamander_MIDL_PROC_FORMAT_STRING;

typedef struct _salamander_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } salamander_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax_2_0 = 
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};

#if defined(_CONTROL_FLOW_GUARD_XFG)
#define XFG_TRAMPOLINES(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree(pFlags, (ObjectType *)pObject);\
}
#define XFG_TRAMPOLINES64(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize64_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize64(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree64_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree64(pFlags, (ObjectType *)pObject);\
}
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)\
static void* ObjectType ## _bind_XFG(HandleType pObject)\
{\
return ObjectType ## _bind((ObjectType) pObject);\
}\
static void ObjectType ## _unbind_XFG(HandleType pObject, handle_t ServerHandle)\
{\
ObjectType ## _unbind((ObjectType) pObject, ServerHandle);\
}
#define XFG_TRAMPOLINE_FPTR(Function) Function ## _XFG
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol ## _XFG
#else
#define XFG_TRAMPOLINES(ObjectType)
#define XFG_TRAMPOLINES64(ObjectType)
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)
#define XFG_TRAMPOLINE_FPTR(Function) Function
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol
#endif


extern const salamander_MIDL_TYPE_FORMAT_STRING salamander__MIDL_TypeFormatString;
extern const salamander_MIDL_PROC_FORMAT_STRING salamander__MIDL_ProcFormatString;
extern const salamander_MIDL_EXPR_FORMAT_STRING salamander__MIDL_ExprFormatString;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamander_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamander_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderPanel_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderPanel_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderPanelItem_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderPanelItem_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderPanelItemCollection_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderPanelItemCollection_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderProgressDialog_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderProgressDialog_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderWaitWindow_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderWaitWindow_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderScriptInfo_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderScriptInfo_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderGuiComponent_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiComponent_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderGuiCheckBox_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiCheckBox_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderGuiButton_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiButton_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderGuiContainer_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiContainer_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderGuiForm_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiForm_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO ISalamanderGui_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO ISalamanderGui_ProxyInfo;


extern const USER_MARSHAL_ROUTINE_QUADRUPLE UserMarshalRoutines[ WIRE_MARSHAL_TABLE_SIZE ];

#if !defined(__RPC_WIN32__)
#error  Invalid build platform for this stub.
#endif
#if !(TARGET_IS_NT60_OR_LATER)
#error You need Windows Vista or later to run this stub because it uses these features:
#error   forced complex structure or array, compiled for Windows Vista.
#error However, your C/C++ compilation flags indicate you intend to run this app on earlier systems.
#error This app will fail with the RPC_X_WRONG_STUB_VERSION error.
#endif


static const salamander_MIDL_PROC_FORMAT_STRING salamander__MIDL_ProcFormatString =
    {
        0,
        {

	/* Procedure MsgBox */

			0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/*  2 */	NdrFcLong( 0x0 ),	/* 0 */
/*  6 */	NdrFcShort( 0x3 ),	/* 3 */
/*  8 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 10 */	NdrFcShort( 0x0 ),	/* 0 */
/* 12 */	NdrFcShort( 0x24 ),	/* 36 */
/* 14 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x5,		/* 5 */
/* 16 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 18 */	NdrFcShort( 0x0 ),	/* 0 */
/* 20 */	NdrFcShort( 0x1 ),	/* 1 */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter prompt */

/* 24 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 26 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 28 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter buttons */

/* 30 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 32 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 34 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter title */

/* 36 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 38 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 40 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter result */

/* 42 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 44 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 46 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 48 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 50 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 52 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_LeftPanel */

/* 54 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 56 */	NdrFcLong( 0x0 ),	/* 0 */
/* 60 */	NdrFcShort( 0x4 ),	/* 4 */
/* 62 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 64 */	NdrFcShort( 0x0 ),	/* 0 */
/* 66 */	NdrFcShort( 0x8 ),	/* 8 */
/* 68 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 70 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 72 */	NdrFcShort( 0x0 ),	/* 0 */
/* 74 */	NdrFcShort( 0x0 ),	/* 0 */
/* 76 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter panel */

/* 78 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 80 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 82 */	NdrFcShort( 0x41c ),	/* Type Offset=1052 */

	/* Return value */

/* 84 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 86 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 88 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_RightPanel */

/* 90 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 92 */	NdrFcLong( 0x0 ),	/* 0 */
/* 96 */	NdrFcShort( 0x5 ),	/* 5 */
/* 98 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 100 */	NdrFcShort( 0x0 ),	/* 0 */
/* 102 */	NdrFcShort( 0x8 ),	/* 8 */
/* 104 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 106 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 108 */	NdrFcShort( 0x0 ),	/* 0 */
/* 110 */	NdrFcShort( 0x0 ),	/* 0 */
/* 112 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter panel */

/* 114 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 116 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 118 */	NdrFcShort( 0x41c ),	/* Type Offset=1052 */

	/* Return value */

/* 120 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 122 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 124 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_SourcePanel */

/* 126 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 128 */	NdrFcLong( 0x0 ),	/* 0 */
/* 132 */	NdrFcShort( 0x6 ),	/* 6 */
/* 134 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 136 */	NdrFcShort( 0x0 ),	/* 0 */
/* 138 */	NdrFcShort( 0x8 ),	/* 8 */
/* 140 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 142 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 144 */	NdrFcShort( 0x0 ),	/* 0 */
/* 146 */	NdrFcShort( 0x0 ),	/* 0 */
/* 148 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter panel */

/* 150 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 152 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 154 */	NdrFcShort( 0x41c ),	/* Type Offset=1052 */

	/* Return value */

/* 156 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 158 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 160 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_TargetPanel */

/* 162 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 164 */	NdrFcLong( 0x0 ),	/* 0 */
/* 168 */	NdrFcShort( 0x7 ),	/* 7 */
/* 170 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 172 */	NdrFcShort( 0x0 ),	/* 0 */
/* 174 */	NdrFcShort( 0x8 ),	/* 8 */
/* 176 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 178 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 180 */	NdrFcShort( 0x0 ),	/* 0 */
/* 182 */	NdrFcShort( 0x0 ),	/* 0 */
/* 184 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter panel */

/* 186 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 188 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 190 */	NdrFcShort( 0x41c ),	/* Type Offset=1052 */

	/* Return value */

/* 192 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 194 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 196 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Version */

/* 198 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 200 */	NdrFcLong( 0x0 ),	/* 0 */
/* 204 */	NdrFcShort( 0x8 ),	/* 8 */
/* 206 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 208 */	NdrFcShort( 0x0 ),	/* 0 */
/* 210 */	NdrFcShort( 0x24 ),	/* 36 */
/* 212 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 214 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 216 */	NdrFcShort( 0x0 ),	/* 0 */
/* 218 */	NdrFcShort( 0x0 ),	/* 0 */
/* 220 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter version */

/* 222 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 224 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 226 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 228 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 230 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 232 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure InputBox */

/* 234 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 236 */	NdrFcLong( 0x0 ),	/* 0 */
/* 240 */	NdrFcShort( 0x9 ),	/* 9 */
/* 242 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 244 */	NdrFcShort( 0x0 ),	/* 0 */
/* 246 */	NdrFcShort( 0x8 ),	/* 8 */
/* 248 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x5,		/* 5 */
/* 250 */	0x8,		/* 8 */
			0x7,		/* Ext Flags:  new corr desc, clt corr check, srv corr check, */
/* 252 */	NdrFcShort( 0x1 ),	/* 1 */
/* 254 */	NdrFcShort( 0x1 ),	/* 1 */
/* 256 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter prompt */

/* 258 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 260 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 262 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter title */

/* 264 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 266 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 268 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter _default */

/* 270 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 272 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 274 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter result */

/* 276 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 278 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 280 */	NdrFcShort( 0x43a ),	/* Type Offset=1082 */

	/* Return value */

/* 282 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 284 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 286 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Delay */


	/* Procedure get_PathType */


	/* Procedure get_WindowsVersion */

/* 288 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 290 */	NdrFcLong( 0x0 ),	/* 0 */
/* 294 */	NdrFcShort( 0xa ),	/* 10 */
/* 296 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 298 */	NdrFcShort( 0x0 ),	/* 0 */
/* 300 */	NdrFcShort( 0x24 ),	/* 36 */
/* 302 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 304 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 306 */	NdrFcShort( 0x0 ),	/* 0 */
/* 308 */	NdrFcShort( 0x0 ),	/* 0 */
/* 310 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter ms */


	/* Parameter type */


	/* Parameter version */

/* 312 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 314 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 316 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */


	/* Return value */

/* 318 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 320 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 322 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_AutomationVersion */

/* 324 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 326 */	NdrFcLong( 0x0 ),	/* 0 */
/* 330 */	NdrFcShort( 0xb ),	/* 11 */
/* 332 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 334 */	NdrFcShort( 0x0 ),	/* 0 */
/* 336 */	NdrFcShort( 0x24 ),	/* 36 */
/* 338 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 340 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 342 */	NdrFcShort( 0x0 ),	/* 0 */
/* 344 */	NdrFcShort( 0x0 ),	/* 0 */
/* 346 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter version */

/* 348 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 350 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 352 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 354 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 356 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 358 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Sleep */

/* 360 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 362 */	NdrFcLong( 0x0 ),	/* 0 */
/* 366 */	NdrFcShort( 0xc ),	/* 12 */
/* 368 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 370 */	NdrFcShort( 0x8 ),	/* 8 */
/* 372 */	NdrFcShort( 0x8 ),	/* 8 */
/* 374 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 376 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 378 */	NdrFcShort( 0x0 ),	/* 0 */
/* 380 */	NdrFcShort( 0x0 ),	/* 0 */
/* 382 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter time */

/* 384 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 386 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 388 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 390 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 392 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 394 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure AbortScript */

/* 396 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 398 */	NdrFcLong( 0x0 ),	/* 0 */
/* 402 */	NdrFcShort( 0xd ),	/* 13 */
/* 404 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 406 */	NdrFcShort( 0x0 ),	/* 0 */
/* 408 */	NdrFcShort( 0x8 ),	/* 8 */
/* 410 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 412 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 414 */	NdrFcShort( 0x0 ),	/* 0 */
/* 416 */	NdrFcShort( 0x0 ),	/* 0 */
/* 418 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 420 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 422 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 424 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure TraceI */

/* 426 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 428 */	NdrFcLong( 0x0 ),	/* 0 */
/* 432 */	NdrFcShort( 0xe ),	/* 14 */
/* 434 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 436 */	NdrFcShort( 0x0 ),	/* 0 */
/* 438 */	NdrFcShort( 0x8 ),	/* 8 */
/* 440 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 442 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 444 */	NdrFcShort( 0x0 ),	/* 0 */
/* 446 */	NdrFcShort( 0x1 ),	/* 1 */
/* 448 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter message */

/* 450 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 452 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 454 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Return value */

/* 456 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 458 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 460 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure TraceE */

/* 462 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 464 */	NdrFcLong( 0x0 ),	/* 0 */
/* 468 */	NdrFcShort( 0xf ),	/* 15 */
/* 470 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 472 */	NdrFcShort( 0x0 ),	/* 0 */
/* 474 */	NdrFcShort( 0x8 ),	/* 8 */
/* 476 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 478 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 480 */	NdrFcShort( 0x0 ),	/* 0 */
/* 482 */	NdrFcShort( 0x1 ),	/* 1 */
/* 484 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter message */

/* 486 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 488 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 490 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Return value */

/* 492 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 494 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 496 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_ProgressDialog */

/* 498 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 500 */	NdrFcLong( 0x0 ),	/* 0 */
/* 504 */	NdrFcShort( 0x10 ),	/* 16 */
/* 506 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 508 */	NdrFcShort( 0x0 ),	/* 0 */
/* 510 */	NdrFcShort( 0x8 ),	/* 8 */
/* 512 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 514 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 516 */	NdrFcShort( 0x0 ),	/* 0 */
/* 518 */	NdrFcShort( 0x0 ),	/* 0 */
/* 520 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter dialog */

/* 522 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 524 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 526 */	NdrFcShort( 0x444 ),	/* Type Offset=1092 */

	/* Return value */

/* 528 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 530 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 532 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_WaitWindow */

/* 534 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 536 */	NdrFcLong( 0x0 ),	/* 0 */
/* 540 */	NdrFcShort( 0x11 ),	/* 17 */
/* 542 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 544 */	NdrFcShort( 0x0 ),	/* 0 */
/* 546 */	NdrFcShort( 0x8 ),	/* 8 */
/* 548 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 550 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 552 */	NdrFcShort( 0x0 ),	/* 0 */
/* 554 */	NdrFcShort( 0x0 ),	/* 0 */
/* 556 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter window */

/* 558 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 560 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 562 */	NdrFcShort( 0x45a ),	/* Type Offset=1114 */

	/* Return value */

/* 564 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 566 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 568 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure ViewFile */

/* 570 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 572 */	NdrFcLong( 0x0 ),	/* 0 */
/* 576 */	NdrFcShort( 0x12 ),	/* 18 */
/* 578 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 580 */	NdrFcShort( 0x0 ),	/* 0 */
/* 582 */	NdrFcShort( 0x8 ),	/* 8 */
/* 584 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 586 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 588 */	NdrFcShort( 0x0 ),	/* 0 */
/* 590 */	NdrFcShort( 0x1 ),	/* 1 */
/* 592 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter file */

/* 594 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 596 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 598 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter cache */

/* 600 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 602 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 604 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Return value */

/* 606 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 608 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 610 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure MatchesMask */

/* 612 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 614 */	NdrFcLong( 0x0 ),	/* 0 */
/* 618 */	NdrFcShort( 0x13 ),	/* 19 */
/* 620 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 622 */	NdrFcShort( 0x0 ),	/* 0 */
/* 624 */	NdrFcShort( 0x22 ),	/* 34 */
/* 626 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 628 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 630 */	NdrFcShort( 0x0 ),	/* 0 */
/* 632 */	NdrFcShort( 0x1 ),	/* 1 */
/* 634 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter file */

/* 636 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 638 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 640 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter mask */

/* 642 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 644 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 646 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter match */

/* 648 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 650 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 652 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 654 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 656 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 658 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure DebugBreak */

/* 660 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 662 */	NdrFcLong( 0x0 ),	/* 0 */
/* 666 */	NdrFcShort( 0x14 ),	/* 20 */
/* 668 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 670 */	NdrFcShort( 0x0 ),	/* 0 */
/* 672 */	NdrFcShort( 0x8 ),	/* 8 */
/* 674 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 676 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 678 */	NdrFcShort( 0x0 ),	/* 0 */
/* 680 */	NdrFcShort( 0x0 ),	/* 0 */
/* 682 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 684 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 686 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 688 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure ErrorDialog */

/* 690 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 692 */	NdrFcLong( 0x0 ),	/* 0 */
/* 696 */	NdrFcShort( 0x15 ),	/* 21 */
/* 698 */	NdrFcShort( 0x1c ),	/* x86 Stack size/offset = 28 */
/* 700 */	NdrFcShort( 0x8 ),	/* 8 */
/* 702 */	NdrFcShort( 0x24 ),	/* 36 */
/* 704 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x6,		/* 6 */
/* 706 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 708 */	NdrFcShort( 0x0 ),	/* 0 */
/* 710 */	NdrFcShort( 0x1 ),	/* 1 */
/* 712 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter file */

/* 714 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 716 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 718 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter error */

/* 720 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 722 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 724 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter buttons */

/* 726 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 728 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 730 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter title */

/* 732 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 734 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 736 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter result */

/* 738 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 740 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 742 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 744 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 746 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 748 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure QuestionDialog */

/* 750 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 752 */	NdrFcLong( 0x0 ),	/* 0 */
/* 756 */	NdrFcShort( 0x16 ),	/* 22 */
/* 758 */	NdrFcShort( 0x1c ),	/* x86 Stack size/offset = 28 */
/* 760 */	NdrFcShort( 0x8 ),	/* 8 */
/* 762 */	NdrFcShort( 0x24 ),	/* 36 */
/* 764 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x6,		/* 6 */
/* 766 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 768 */	NdrFcShort( 0x0 ),	/* 0 */
/* 770 */	NdrFcShort( 0x1 ),	/* 1 */
/* 772 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter file */

/* 774 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 776 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 778 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter question */

/* 780 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 782 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 784 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter buttons */

/* 786 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 788 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 790 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter title */

/* 792 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 794 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 796 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter result */

/* 798 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 800 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 802 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 804 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 806 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 808 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure OverwriteDialog */

/* 810 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 812 */	NdrFcLong( 0x0 ),	/* 0 */
/* 816 */	NdrFcShort( 0x17 ),	/* 23 */
/* 818 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 820 */	NdrFcShort( 0x8 ),	/* 8 */
/* 822 */	NdrFcShort( 0x24 ),	/* 36 */
/* 824 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x5,		/* 5 */
/* 826 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 828 */	NdrFcShort( 0x0 ),	/* 0 */
/* 830 */	NdrFcShort( 0x1 ),	/* 1 */
/* 832 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter file1 */

/* 834 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 836 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 838 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter file2 */

/* 840 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 842 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 844 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter buttons */

/* 846 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 848 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 850 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Parameter result */

/* 852 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 854 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 856 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 858 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 860 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 862 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Forms */

/* 864 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 866 */	NdrFcLong( 0x0 ),	/* 0 */
/* 870 */	NdrFcShort( 0x18 ),	/* 24 */
/* 872 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 874 */	NdrFcShort( 0x0 ),	/* 0 */
/* 876 */	NdrFcShort( 0x8 ),	/* 8 */
/* 878 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 880 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 882 */	NdrFcShort( 0x0 ),	/* 0 */
/* 884 */	NdrFcShort( 0x0 ),	/* 0 */
/* 886 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter gui */

/* 888 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 890 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 892 */	NdrFcShort( 0x474 ),	/* Type Offset=1140 */

	/* Return value */

/* 894 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 896 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 898 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure SetPersistentVal */

/* 900 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 902 */	NdrFcLong( 0x0 ),	/* 0 */
/* 906 */	NdrFcShort( 0x19 ),	/* 25 */
/* 908 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 910 */	NdrFcShort( 0x0 ),	/* 0 */
/* 912 */	NdrFcShort( 0x8 ),	/* 8 */
/* 914 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 916 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 918 */	NdrFcShort( 0x0 ),	/* 0 */
/* 920 */	NdrFcShort( 0x1 ),	/* 1 */
/* 922 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter key */

/* 924 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 926 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 928 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter val */

/* 930 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 932 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 934 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Return value */

/* 936 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 938 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 940 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure GetPersistentVal */

/* 942 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 944 */	NdrFcLong( 0x0 ),	/* 0 */
/* 948 */	NdrFcShort( 0x1a ),	/* 26 */
/* 950 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 952 */	NdrFcShort( 0x0 ),	/* 0 */
/* 954 */	NdrFcShort( 0x8 ),	/* 8 */
/* 956 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 958 */	0x8,		/* 8 */
			0x7,		/* Ext Flags:  new corr desc, clt corr check, srv corr check, */
/* 960 */	NdrFcShort( 0x1 ),	/* 1 */
/* 962 */	NdrFcShort( 0x1 ),	/* 1 */
/* 964 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter key */

/* 966 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 968 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 970 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter val */

/* 972 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 974 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 976 */	NdrFcShort( 0x492 ),	/* Type Offset=1170 */

	/* Return value */

/* 978 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 980 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 982 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Script */

/* 984 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 986 */	NdrFcLong( 0x0 ),	/* 0 */
/* 990 */	NdrFcShort( 0x1b ),	/* 27 */
/* 992 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 994 */	NdrFcShort( 0x0 ),	/* 0 */
/* 996 */	NdrFcShort( 0x8 ),	/* 8 */
/* 998 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1000 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1002 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1004 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1006 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter info */

/* 1008 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 1010 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1012 */	NdrFcShort( 0x49c ),	/* Type Offset=1180 */

	/* Return value */

/* 1014 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1016 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1018 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure GetFullPath */

/* 1020 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1022 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1026 */	NdrFcShort( 0x1c ),	/* 28 */
/* 1028 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 1030 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1032 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1034 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 1036 */	0x8,		/* 8 */
			0x7,		/* Ext Flags:  new corr desc, clt corr check, srv corr check, */
/* 1038 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1040 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1042 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter path */

/* 1044 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 1046 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1048 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Parameter panel */

/* 1050 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 1052 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1054 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter result */

/* 1056 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1058 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1060 */	NdrFcShort( 0x43a ),	/* Type Offset=1082 */

	/* Return value */

/* 1062 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1064 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 1066 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Path */

/* 1068 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1070 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1074 */	NdrFcShort( 0x3 ),	/* 3 */
/* 1076 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1078 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1080 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1082 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 1084 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 1086 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1088 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1090 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter path */

/* 1092 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 1094 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1096 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Return value */

/* 1098 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1100 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1102 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Path */


	/* Procedure get_Path */


	/* Procedure get_Path */

/* 1104 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1106 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1110 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1112 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1114 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1116 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1118 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1120 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1122 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1124 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1126 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter path */


	/* Parameter path */


	/* Parameter path */

/* 1128 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1130 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1132 */	NdrFcShort( 0x43a ),	/* Type Offset=1082 */

	/* Return value */


	/* Return value */


	/* Return value */

/* 1134 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1136 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1138 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_FocusedItem */

/* 1140 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1142 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1146 */	NdrFcShort( 0x5 ),	/* 5 */
/* 1148 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1150 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1152 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1154 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1156 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1158 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1160 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1162 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter item */

/* 1164 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 1166 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1168 */	NdrFcShort( 0x4b2 ),	/* Type Offset=1202 */

	/* Return value */

/* 1170 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1172 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1174 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_SelectedItems */

/* 1176 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1178 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1182 */	NdrFcShort( 0x6 ),	/* 6 */
/* 1184 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1186 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1188 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1190 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1192 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1194 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1196 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1198 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter coll */

/* 1200 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 1202 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1204 */	NdrFcShort( 0x4c8 ),	/* Type Offset=1224 */

	/* Return value */

/* 1206 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1208 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1210 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Items */

/* 1212 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1214 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1218 */	NdrFcShort( 0x7 ),	/* 7 */
/* 1220 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1222 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1224 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1226 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1228 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1230 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1232 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1234 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter coll */

/* 1236 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 1238 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1240 */	NdrFcShort( 0x4c8 ),	/* Type Offset=1224 */

	/* Return value */

/* 1242 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1244 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1246 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure SelectAll */

/* 1248 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1250 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1254 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1256 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1258 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1260 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1262 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 1264 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1266 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1268 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1270 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 1272 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1274 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1276 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure DeselectAll */

/* 1278 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1280 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1284 */	NdrFcShort( 0x9 ),	/* 9 */
/* 1286 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1288 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1290 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1292 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 1294 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 1296 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1298 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1300 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter save */

/* 1302 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 1304 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1306 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Return value */

/* 1308 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1310 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1312 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure StoreSelection */

/* 1314 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1316 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1320 */	NdrFcShort( 0xb ),	/* 11 */
/* 1322 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1324 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1326 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1328 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 1330 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1332 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1334 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1336 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 1338 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1340 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1342 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Text */


	/* Procedure get_Name */


	/* Procedure get_Name */

/* 1344 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1346 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1350 */	NdrFcShort( 0x3 ),	/* 3 */
/* 1352 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1354 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1356 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1358 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1360 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1362 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1364 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1366 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */


	/* Parameter name */


	/* Parameter name */

/* 1368 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1370 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1372 */	NdrFcShort( 0x43a ),	/* Type Offset=1082 */

	/* Return value */


	/* Return value */


	/* Return value */

/* 1374 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1376 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1378 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Size */

/* 1380 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1382 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1386 */	NdrFcShort( 0x5 ),	/* 5 */
/* 1388 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1390 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1392 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1394 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1396 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1398 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1400 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1402 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter size */

/* 1404 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 1406 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1408 */	NdrFcShort( 0x492 ),	/* Type Offset=1170 */

	/* Return value */

/* 1410 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1412 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1414 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_DateLastModified */

/* 1416 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1418 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1422 */	NdrFcShort( 0x6 ),	/* 6 */
/* 1424 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1426 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1428 */	NdrFcShort( 0x2c ),	/* 44 */
/* 1430 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1432 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1434 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1436 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1438 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter date */

/* 1440 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1442 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1444 */	0xc,		/* FC_DOUBLE */
			0x0,		/* 0 */

	/* Return value */

/* 1446 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1448 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1450 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Attributes */

/* 1452 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1454 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1458 */	NdrFcShort( 0x7 ),	/* 7 */
/* 1460 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1462 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1464 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1466 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1468 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1470 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1472 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1474 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter attrs */

/* 1476 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1478 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1480 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1482 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1484 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1486 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Item */

/* 1488 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1490 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1494 */	NdrFcShort( 0x3 ),	/* 3 */
/* 1496 */	NdrFcShort( 0x1c ),	/* x86 Stack size/offset = 28 */
/* 1498 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1500 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1502 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 1504 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 1506 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1508 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1510 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter key */

/* 1512 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 1514 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1516 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter item */

/* 1518 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 1520 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 1522 */	NdrFcShort( 0x4b2 ),	/* Type Offset=1202 */

	/* Return value */

/* 1524 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1526 */	NdrFcShort( 0x18 ),	/* x86 Stack size/offset = 24 */
/* 1528 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Count */

/* 1530 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1532 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1536 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1538 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1540 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1542 */	NdrFcShort( 0x24 ),	/* 36 */
/* 1544 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1546 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1548 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1550 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1552 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter count */

/* 1554 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1556 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1558 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1560 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1562 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1564 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get__NewEnum */

/* 1566 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1568 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1572 */	NdrFcShort( 0x5 ),	/* 5 */
/* 1574 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1576 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1578 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1580 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1582 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1584 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1586 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1588 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter ppenum */

/* 1590 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 1592 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1594 */	NdrFcShort( 0x4e2 ),	/* Type Offset=1250 */

	/* Return value */

/* 1596 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1598 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1600 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Show */


	/* Procedure Show */

/* 1602 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1604 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1608 */	NdrFcShort( 0x3 ),	/* 3 */
/* 1610 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1612 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1614 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1616 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 1618 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1620 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1622 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1624 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */


	/* Return value */

/* 1626 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1628 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1630 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Hide */


	/* Procedure Hide */

/* 1632 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1634 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1638 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1640 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1642 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1644 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1646 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 1648 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1650 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1652 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1654 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */


	/* Return value */

/* 1656 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1658 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1660 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Title */


	/* Procedure get_Title */

/* 1662 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1664 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1668 */	NdrFcShort( 0x5 ),	/* 5 */
/* 1670 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1672 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1674 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1676 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1678 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1680 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1682 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1684 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter title */


	/* Parameter title */

/* 1686 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 1688 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1690 */	NdrFcShort( 0x43a ),	/* Type Offset=1082 */

	/* Return value */


	/* Return value */

/* 1692 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1694 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1696 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Title */


	/* Procedure put_Title */

/* 1698 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1700 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1704 */	NdrFcShort( 0x6 ),	/* 6 */
/* 1706 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1708 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1710 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1712 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 1714 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 1716 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1718 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1720 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter title */


	/* Parameter title */

/* 1722 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 1724 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1726 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Return value */


	/* Return value */

/* 1728 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1730 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1732 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure AddText */

/* 1734 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1736 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1740 */	NdrFcShort( 0x7 ),	/* 7 */
/* 1742 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1744 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1746 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1748 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 1750 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 1752 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1754 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1756 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 1758 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 1760 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1762 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Return value */

/* 1764 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1766 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1768 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_IsCancelled */

/* 1770 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1772 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1776 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1778 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1780 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1782 */	NdrFcShort( 0x22 ),	/* 34 */
/* 1784 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1786 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1788 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1790 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1792 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cancelled */

/* 1794 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 1796 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1798 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 1800 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1802 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1804 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Position */

/* 1806 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1808 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1812 */	NdrFcShort( 0x9 ),	/* 9 */
/* 1814 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1816 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1818 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1820 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1822 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1824 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1826 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1828 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter position */

/* 1830 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 1832 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1834 */	NdrFcShort( 0x492 ),	/* Type Offset=1170 */

	/* Return value */

/* 1836 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1838 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1840 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Position */

/* 1842 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1844 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1848 */	NdrFcShort( 0xa ),	/* 10 */
/* 1850 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1852 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1854 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1856 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 1858 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 1860 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1862 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1864 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter position */

/* 1866 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 1868 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1870 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Return value */

/* 1872 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1874 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1876 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_TotalPosition */

/* 1878 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1880 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1884 */	NdrFcShort( 0xb ),	/* 11 */
/* 1886 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1888 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1890 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1892 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 1894 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 1896 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1898 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1900 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter position */

/* 1902 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 1904 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1906 */	NdrFcShort( 0x492 ),	/* Type Offset=1170 */

	/* Return value */

/* 1908 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1910 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1912 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_TotalPosition */

/* 1914 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1916 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1920 */	NdrFcShort( 0xc ),	/* 12 */
/* 1922 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1924 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1926 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1928 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 1930 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 1932 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1934 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1936 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter position */

/* 1938 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 1940 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1942 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Return value */

/* 1944 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1946 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1948 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Step */

/* 1950 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1952 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1956 */	NdrFcShort( 0xd ),	/* 13 */
/* 1958 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1960 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1962 */	NdrFcShort( 0x8 ),	/* 8 */
/* 1964 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 1966 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 1968 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1970 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1972 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter step */

/* 1974 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 1976 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 1978 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 1980 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 1982 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 1984 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_CanCancel */

/* 1986 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 1988 */	NdrFcLong( 0x0 ),	/* 0 */
/* 1992 */	NdrFcShort( 0xe ),	/* 14 */
/* 1994 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 1996 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1998 */	NdrFcShort( 0x22 ),	/* 34 */
/* 2000 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2002 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2004 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2006 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2008 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter enabled */

/* 2010 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2012 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2014 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 2016 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2018 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2020 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_CanCancel */

/* 2022 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2024 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2028 */	NdrFcShort( 0xf ),	/* 15 */
/* 2030 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2032 */	NdrFcShort( 0x6 ),	/* 6 */
/* 2034 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2036 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2038 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2040 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2042 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2044 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter enabled */

/* 2046 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2048 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2050 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 2052 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2054 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2056 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Style */

/* 2058 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2060 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2064 */	NdrFcShort( 0x10 ),	/* 16 */
/* 2066 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2068 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2070 */	NdrFcShort( 0x24 ),	/* 36 */
/* 2072 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2074 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2076 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2078 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2080 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter barcount */

/* 2082 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2084 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2086 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2088 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2090 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2092 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Style */

/* 2094 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2096 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2100 */	NdrFcShort( 0x11 ),	/* 17 */
/* 2102 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2104 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2106 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2108 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2110 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2112 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2114 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2116 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter barcount */

/* 2118 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2120 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2122 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2124 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2126 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2128 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Maximum */

/* 2130 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2132 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2136 */	NdrFcShort( 0x12 ),	/* 18 */
/* 2138 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2140 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2142 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2144 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 2146 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2148 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2150 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2152 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter max */

/* 2154 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 2156 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2158 */	NdrFcShort( 0x492 ),	/* Type Offset=1170 */

	/* Return value */

/* 2160 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2162 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2164 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Maximum */

/* 2166 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2168 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2172 */	NdrFcShort( 0x13 ),	/* 19 */
/* 2174 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2176 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2178 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2180 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 2182 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2184 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2186 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2188 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter max */

/* 2190 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2192 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2194 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Return value */

/* 2196 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2198 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2200 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_TotalMaximum */

/* 2202 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2204 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2208 */	NdrFcShort( 0x14 ),	/* 20 */
/* 2210 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2212 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2214 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2216 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 2218 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2220 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2222 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2224 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter max */

/* 2226 */	NdrFcShort( 0x4113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=16 */
/* 2228 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2230 */	NdrFcShort( 0x492 ),	/* Type Offset=1170 */

	/* Return value */

/* 2232 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2234 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2236 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_TotalMaximum */

/* 2238 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2240 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2244 */	NdrFcShort( 0x15 ),	/* 21 */
/* 2246 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2248 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2250 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2252 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 2254 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2256 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2258 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2260 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter max */

/* 2262 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2264 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2266 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Return value */

/* 2268 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2270 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2272 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Text */

/* 2274 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2276 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2280 */	NdrFcShort( 0x7 ),	/* 7 */
/* 2282 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2284 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2286 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2288 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 2290 */	0x8,		/* 8 */
			0x3,		/* Ext Flags:  new corr desc, clt corr check, */
/* 2292 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2294 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2296 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 2298 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 2300 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2302 */	NdrFcShort( 0x43a ),	/* Type Offset=1082 */

	/* Return value */

/* 2304 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2306 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2308 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Text */

/* 2310 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2312 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2316 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2318 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2320 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2322 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2324 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 2326 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2328 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2330 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2332 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 2334 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 2336 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2338 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Return value */

/* 2340 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2342 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2344 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_IsCancelled */

/* 2346 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2348 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2352 */	NdrFcShort( 0x9 ),	/* 9 */
/* 2354 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2356 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2358 */	NdrFcShort( 0x22 ),	/* 34 */
/* 2360 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2362 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2364 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2366 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2368 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter cancelled */

/* 2370 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2372 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2374 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 2376 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2378 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2380 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Delay */

/* 2382 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2384 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2388 */	NdrFcShort( 0xb ),	/* 11 */
/* 2390 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2392 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2394 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2396 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2398 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2400 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2402 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2404 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter ms */

/* 2406 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2408 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2410 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2412 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2414 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2416 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_CanCancel */

/* 2418 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2420 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2424 */	NdrFcShort( 0xc ),	/* 12 */
/* 2426 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2428 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2430 */	NdrFcShort( 0x22 ),	/* 34 */
/* 2432 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2434 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2436 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2438 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2440 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter enabled */

/* 2442 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2444 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2446 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 2448 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2450 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2452 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_CanCancel */

/* 2454 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2456 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2460 */	NdrFcShort( 0xd ),	/* 13 */
/* 2462 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2464 */	NdrFcShort( 0x6 ),	/* 6 */
/* 2466 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2468 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2470 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2472 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2474 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2476 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter enabled */

/* 2478 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2480 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2482 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 2484 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2486 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2488 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Text */

/* 2490 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2492 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2496 */	NdrFcShort( 0x4 ),	/* 4 */
/* 2498 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2500 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2502 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2504 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x2,		/* 2 */
/* 2506 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2508 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2510 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2512 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 2514 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 2516 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2518 */	NdrFcShort( 0x1c ),	/* Type Offset=28 */

	/* Return value */

/* 2520 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2522 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2524 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure get_Checked */

/* 2526 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2528 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2532 */	NdrFcShort( 0x5 ),	/* 5 */
/* 2534 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2536 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2538 */	NdrFcShort( 0x22 ),	/* 34 */
/* 2540 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2542 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2544 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2546 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2548 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter val */

/* 2550 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2552 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2554 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 2556 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2558 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2560 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_Checked */

/* 2562 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2564 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2568 */	NdrFcShort( 0x6 ),	/* 6 */
/* 2570 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2572 */	NdrFcShort( 0x6 ),	/* 6 */
/* 2574 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2576 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2578 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2580 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2582 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2584 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter val */

/* 2586 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2588 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2590 */	0x6,		/* FC_SHORT */
			0x0,		/* 0 */

	/* Return value */

/* 2592 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2594 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2596 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Execute */


	/* Procedure get_DialogResult */

/* 2598 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2600 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2604 */	NdrFcShort( 0x5 ),	/* 5 */
/* 2606 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2608 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2610 */	NdrFcShort( 0x24 ),	/* 36 */
/* 2612 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2614 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2616 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2618 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2620 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter result */


	/* Parameter val */

/* 2622 */	NdrFcShort( 0x2150 ),	/* Flags:  out, base type, simple ref, srv alloc size=8 */
/* 2624 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2626 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */


	/* Return value */

/* 2628 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2630 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2632 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure put_DialogResult */

/* 2634 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2636 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2640 */	NdrFcShort( 0x6 ),	/* 6 */
/* 2642 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2644 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2646 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2648 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 2650 */	0x8,		/* 8 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 2652 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2654 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2656 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter val */

/* 2658 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 2660 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2662 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Return value */

/* 2664 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2666 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2668 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Form */

/* 2670 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2672 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2676 */	NdrFcShort( 0x3 ),	/* 3 */
/* 2678 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 2680 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2682 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2684 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 2686 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2688 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2690 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2692 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter title */

/* 2694 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2696 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2698 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter component */

/* 2700 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 2702 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2704 */	NdrFcShort( 0x4e6 ),	/* Type Offset=1254 */

	/* Return value */

/* 2706 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2708 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2710 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Button */

/* 2712 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2714 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2718 */	NdrFcShort( 0x4 ),	/* 4 */
/* 2720 */	NdrFcShort( 0x14 ),	/* x86 Stack size/offset = 20 */
/* 2722 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2724 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2726 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 2728 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2730 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2732 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2734 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 2736 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2738 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2740 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter result */

/* 2742 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2744 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2746 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter component */

/* 2748 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 2750 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2752 */	NdrFcShort( 0x4e6 ),	/* Type Offset=1254 */

	/* Return value */

/* 2754 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2756 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 2758 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure CheckBox */

/* 2760 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2762 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2766 */	NdrFcShort( 0x5 ),	/* 5 */
/* 2768 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 2770 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2772 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2774 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 2776 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2778 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2780 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2782 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 2784 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2786 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2788 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter component */

/* 2790 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 2792 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2794 */	NdrFcShort( 0x4e6 ),	/* Type Offset=1254 */

	/* Return value */

/* 2796 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2798 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2800 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure TextBox */

/* 2802 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2804 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2808 */	NdrFcShort( 0x6 ),	/* 6 */
/* 2810 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 2812 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2814 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2816 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 2818 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2820 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2822 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2824 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 2826 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2828 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2830 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter component */

/* 2832 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 2834 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2836 */	NdrFcShort( 0x4e6 ),	/* Type Offset=1254 */

	/* Return value */

/* 2838 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2840 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2842 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

	/* Procedure Label */

/* 2844 */	0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/* 2846 */	NdrFcLong( 0x0 ),	/* 0 */
/* 2850 */	NdrFcShort( 0x7 ),	/* 7 */
/* 2852 */	NdrFcShort( 0x10 ),	/* x86 Stack size/offset = 16 */
/* 2854 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2856 */	NdrFcShort( 0x8 ),	/* 8 */
/* 2858 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 2860 */	0x8,		/* 8 */
			0x5,		/* Ext Flags:  new corr desc, srv corr check, */
/* 2862 */	NdrFcShort( 0x0 ),	/* 0 */
/* 2864 */	NdrFcShort( 0x1 ),	/* 1 */
/* 2866 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter text */

/* 2868 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 2870 */	NdrFcShort( 0x4 ),	/* x86 Stack size/offset = 4 */
/* 2872 */	NdrFcShort( 0x40e ),	/* Type Offset=1038 */

	/* Parameter component */

/* 2874 */	NdrFcShort( 0x13 ),	/* Flags:  must size, must free, out, */
/* 2876 */	NdrFcShort( 0x8 ),	/* x86 Stack size/offset = 8 */
/* 2878 */	NdrFcShort( 0x4e6 ),	/* Type Offset=1254 */

	/* Return value */

/* 2880 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 2882 */	NdrFcShort( 0xc ),	/* x86 Stack size/offset = 12 */
/* 2884 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

			0x0
        }
    };

static const salamander_MIDL_TYPE_FORMAT_STRING salamander__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */
/*  2 */	
			0x12, 0x0,	/* FC_UP */
/*  4 */	NdrFcShort( 0xe ),	/* Offset= 14 (18) */
/*  6 */	
			0x1b,		/* FC_CARRAY */
			0x1,		/* 1 */
/*  8 */	NdrFcShort( 0x2 ),	/* 2 */
/* 10 */	0x9,		/* Corr desc: FC_ULONG */
			0x0,		/*  */
/* 12 */	NdrFcShort( 0xfffc ),	/* -4 */
/* 14 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 16 */	0x6,		/* FC_SHORT */
			0x5b,		/* FC_END */
/* 18 */	
			0x17,		/* FC_CSTRUCT */
			0x3,		/* 3 */
/* 20 */	NdrFcShort( 0x8 ),	/* 8 */
/* 22 */	NdrFcShort( 0xfff0 ),	/* Offset= -16 (6) */
/* 24 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 26 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 28 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 30 */	NdrFcShort( 0x0 ),	/* 0 */
/* 32 */	NdrFcShort( 0x4 ),	/* 4 */
/* 34 */	NdrFcShort( 0x0 ),	/* 0 */
/* 36 */	NdrFcShort( 0xffde ),	/* Offset= -34 (2) */
/* 38 */	
			0x11, 0x0,	/* FC_RP */
/* 40 */	NdrFcShort( 0x3e6 ),	/* Offset= 998 (1038) */
/* 42 */	
			0x12, 0x0,	/* FC_UP */
/* 44 */	NdrFcShort( 0x3ce ),	/* Offset= 974 (1018) */
/* 46 */	
			0x2b,		/* FC_NON_ENCAPSULATED_UNION */
			0x9,		/* FC_ULONG */
/* 48 */	0x7,		/* Corr desc: FC_USHORT */
			0x0,		/*  */
/* 50 */	NdrFcShort( 0xfff8 ),	/* -8 */
/* 52 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 54 */	NdrFcShort( 0x2 ),	/* Offset= 2 (56) */
/* 56 */	NdrFcShort( 0x10 ),	/* 16 */
/* 58 */	NdrFcShort( 0x2f ),	/* 47 */
/* 60 */	NdrFcLong( 0x14 ),	/* 20 */
/* 64 */	NdrFcShort( 0x800b ),	/* Simple arm type: FC_HYPER */
/* 66 */	NdrFcLong( 0x3 ),	/* 3 */
/* 70 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 72 */	NdrFcLong( 0x11 ),	/* 17 */
/* 76 */	NdrFcShort( 0x8001 ),	/* Simple arm type: FC_BYTE */
/* 78 */	NdrFcLong( 0x2 ),	/* 2 */
/* 82 */	NdrFcShort( 0x8006 ),	/* Simple arm type: FC_SHORT */
/* 84 */	NdrFcLong( 0x4 ),	/* 4 */
/* 88 */	NdrFcShort( 0x800a ),	/* Simple arm type: FC_FLOAT */
/* 90 */	NdrFcLong( 0x5 ),	/* 5 */
/* 94 */	NdrFcShort( 0x800c ),	/* Simple arm type: FC_DOUBLE */
/* 96 */	NdrFcLong( 0xb ),	/* 11 */
/* 100 */	NdrFcShort( 0x8006 ),	/* Simple arm type: FC_SHORT */
/* 102 */	NdrFcLong( 0xa ),	/* 10 */
/* 106 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 108 */	NdrFcLong( 0x6 ),	/* 6 */
/* 112 */	NdrFcShort( 0xe8 ),	/* Offset= 232 (344) */
/* 114 */	NdrFcLong( 0x7 ),	/* 7 */
/* 118 */	NdrFcShort( 0x800c ),	/* Simple arm type: FC_DOUBLE */
/* 120 */	NdrFcLong( 0x8 ),	/* 8 */
/* 124 */	NdrFcShort( 0xff86 ),	/* Offset= -122 (2) */
/* 126 */	NdrFcLong( 0xd ),	/* 13 */
/* 130 */	NdrFcShort( 0xdc ),	/* Offset= 220 (350) */
/* 132 */	NdrFcLong( 0x9 ),	/* 9 */
/* 136 */	NdrFcShort( 0xe8 ),	/* Offset= 232 (368) */
/* 138 */	NdrFcLong( 0x2000 ),	/* 8192 */
/* 142 */	NdrFcShort( 0xf4 ),	/* Offset= 244 (386) */
/* 144 */	NdrFcLong( 0x24 ),	/* 36 */
/* 148 */	NdrFcShort( 0x31c ),	/* Offset= 796 (944) */
/* 150 */	NdrFcLong( 0x4024 ),	/* 16420 */
/* 154 */	NdrFcShort( 0x316 ),	/* Offset= 790 (944) */
/* 156 */	NdrFcLong( 0x4011 ),	/* 16401 */
/* 160 */	NdrFcShort( 0x314 ),	/* Offset= 788 (948) */
/* 162 */	NdrFcLong( 0x4002 ),	/* 16386 */
/* 166 */	NdrFcShort( 0x312 ),	/* Offset= 786 (952) */
/* 168 */	NdrFcLong( 0x4003 ),	/* 16387 */
/* 172 */	NdrFcShort( 0x310 ),	/* Offset= 784 (956) */
/* 174 */	NdrFcLong( 0x4014 ),	/* 16404 */
/* 178 */	NdrFcShort( 0x30e ),	/* Offset= 782 (960) */
/* 180 */	NdrFcLong( 0x4004 ),	/* 16388 */
/* 184 */	NdrFcShort( 0x30c ),	/* Offset= 780 (964) */
/* 186 */	NdrFcLong( 0x4005 ),	/* 16389 */
/* 190 */	NdrFcShort( 0x30a ),	/* Offset= 778 (968) */
/* 192 */	NdrFcLong( 0x400b ),	/* 16395 */
/* 196 */	NdrFcShort( 0x2f4 ),	/* Offset= 756 (952) */
/* 198 */	NdrFcLong( 0x400a ),	/* 16394 */
/* 202 */	NdrFcShort( 0x2f2 ),	/* Offset= 754 (956) */
/* 204 */	NdrFcLong( 0x4006 ),	/* 16390 */
/* 208 */	NdrFcShort( 0x2fc ),	/* Offset= 764 (972) */
/* 210 */	NdrFcLong( 0x4007 ),	/* 16391 */
/* 214 */	NdrFcShort( 0x2f2 ),	/* Offset= 754 (968) */
/* 216 */	NdrFcLong( 0x4008 ),	/* 16392 */
/* 220 */	NdrFcShort( 0x2f4 ),	/* Offset= 756 (976) */
/* 222 */	NdrFcLong( 0x400d ),	/* 16397 */
/* 226 */	NdrFcShort( 0x2f2 ),	/* Offset= 754 (980) */
/* 228 */	NdrFcLong( 0x4009 ),	/* 16393 */
/* 232 */	NdrFcShort( 0x2f0 ),	/* Offset= 752 (984) */
/* 234 */	NdrFcLong( 0x6000 ),	/* 24576 */
/* 238 */	NdrFcShort( 0x2ee ),	/* Offset= 750 (988) */
/* 240 */	NdrFcLong( 0x400c ),	/* 16396 */
/* 244 */	NdrFcShort( 0x2ec ),	/* Offset= 748 (992) */
/* 246 */	NdrFcLong( 0x10 ),	/* 16 */
/* 250 */	NdrFcShort( 0x8002 ),	/* Simple arm type: FC_CHAR */
/* 252 */	NdrFcLong( 0x12 ),	/* 18 */
/* 256 */	NdrFcShort( 0x8006 ),	/* Simple arm type: FC_SHORT */
/* 258 */	NdrFcLong( 0x13 ),	/* 19 */
/* 262 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 264 */	NdrFcLong( 0x15 ),	/* 21 */
/* 268 */	NdrFcShort( 0x800b ),	/* Simple arm type: FC_HYPER */
/* 270 */	NdrFcLong( 0x16 ),	/* 22 */
/* 274 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 276 */	NdrFcLong( 0x17 ),	/* 23 */
/* 280 */	NdrFcShort( 0x8008 ),	/* Simple arm type: FC_LONG */
/* 282 */	NdrFcLong( 0xe ),	/* 14 */
/* 286 */	NdrFcShort( 0x2ca ),	/* Offset= 714 (1000) */
/* 288 */	NdrFcLong( 0x400e ),	/* 16398 */
/* 292 */	NdrFcShort( 0x2ce ),	/* Offset= 718 (1010) */
/* 294 */	NdrFcLong( 0x4010 ),	/* 16400 */
/* 298 */	NdrFcShort( 0x2cc ),	/* Offset= 716 (1014) */
/* 300 */	NdrFcLong( 0x4012 ),	/* 16402 */
/* 304 */	NdrFcShort( 0x288 ),	/* Offset= 648 (952) */
/* 306 */	NdrFcLong( 0x4013 ),	/* 16403 */
/* 310 */	NdrFcShort( 0x286 ),	/* Offset= 646 (956) */
/* 312 */	NdrFcLong( 0x4015 ),	/* 16405 */
/* 316 */	NdrFcShort( 0x284 ),	/* Offset= 644 (960) */
/* 318 */	NdrFcLong( 0x4016 ),	/* 16406 */
/* 322 */	NdrFcShort( 0x27a ),	/* Offset= 634 (956) */
/* 324 */	NdrFcLong( 0x4017 ),	/* 16407 */
/* 328 */	NdrFcShort( 0x274 ),	/* Offset= 628 (956) */
/* 330 */	NdrFcLong( 0x0 ),	/* 0 */
/* 334 */	NdrFcShort( 0x0 ),	/* Offset= 0 (334) */
/* 336 */	NdrFcLong( 0x1 ),	/* 1 */
/* 340 */	NdrFcShort( 0x0 ),	/* Offset= 0 (340) */
/* 342 */	NdrFcShort( 0xffff ),	/* Offset= -1 (341) */
/* 344 */	
			0x15,		/* FC_STRUCT */
			0x7,		/* 7 */
/* 346 */	NdrFcShort( 0x8 ),	/* 8 */
/* 348 */	0xb,		/* FC_HYPER */
			0x5b,		/* FC_END */
/* 350 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 352 */	NdrFcLong( 0x0 ),	/* 0 */
/* 356 */	NdrFcShort( 0x0 ),	/* 0 */
/* 358 */	NdrFcShort( 0x0 ),	/* 0 */
/* 360 */	0xc0,		/* 192 */
			0x0,		/* 0 */
/* 362 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 364 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 366 */	0x0,		/* 0 */
			0x46,		/* 70 */
/* 368 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 370 */	NdrFcLong( 0x20400 ),	/* 132096 */
/* 374 */	NdrFcShort( 0x0 ),	/* 0 */
/* 376 */	NdrFcShort( 0x0 ),	/* 0 */
/* 378 */	0xc0,		/* 192 */
			0x0,		/* 0 */
/* 380 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 382 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 384 */	0x0,		/* 0 */
			0x46,		/* 70 */
/* 386 */	
			0x12, 0x10,	/* FC_UP [pointer_deref] */
/* 388 */	NdrFcShort( 0x2 ),	/* Offset= 2 (390) */
/* 390 */	
			0x12, 0x0,	/* FC_UP */
/* 392 */	NdrFcShort( 0x216 ),	/* Offset= 534 (926) */
/* 394 */	
			0x2a,		/* FC_ENCAPSULATED_UNION */
			0x49,		/* 73 */
/* 396 */	NdrFcShort( 0x18 ),	/* 24 */
/* 398 */	NdrFcShort( 0xa ),	/* 10 */
/* 400 */	NdrFcLong( 0x8 ),	/* 8 */
/* 404 */	NdrFcShort( 0x5a ),	/* Offset= 90 (494) */
/* 406 */	NdrFcLong( 0xd ),	/* 13 */
/* 410 */	NdrFcShort( 0x7e ),	/* Offset= 126 (536) */
/* 412 */	NdrFcLong( 0x9 ),	/* 9 */
/* 416 */	NdrFcShort( 0x9e ),	/* Offset= 158 (574) */
/* 418 */	NdrFcLong( 0xc ),	/* 12 */
/* 422 */	NdrFcShort( 0xc8 ),	/* Offset= 200 (622) */
/* 424 */	NdrFcLong( 0x24 ),	/* 36 */
/* 428 */	NdrFcShort( 0x124 ),	/* Offset= 292 (720) */
/* 430 */	NdrFcLong( 0x800d ),	/* 32781 */
/* 434 */	NdrFcShort( 0x140 ),	/* Offset= 320 (754) */
/* 436 */	NdrFcLong( 0x10 ),	/* 16 */
/* 440 */	NdrFcShort( 0x15a ),	/* Offset= 346 (786) */
/* 442 */	NdrFcLong( 0x2 ),	/* 2 */
/* 446 */	NdrFcShort( 0x174 ),	/* Offset= 372 (818) */
/* 448 */	NdrFcLong( 0x3 ),	/* 3 */
/* 452 */	NdrFcShort( 0x18e ),	/* Offset= 398 (850) */
/* 454 */	NdrFcLong( 0x14 ),	/* 20 */
/* 458 */	NdrFcShort( 0x1a8 ),	/* Offset= 424 (882) */
/* 460 */	NdrFcShort( 0xffff ),	/* Offset= -1 (459) */
/* 462 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 464 */	NdrFcShort( 0x4 ),	/* 4 */
/* 466 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 468 */	NdrFcShort( 0x0 ),	/* 0 */
/* 470 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 472 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 474 */	
			0x48,		/* FC_VARIABLE_REPEAT */
			0x49,		/* FC_FIXED_OFFSET */
/* 476 */	NdrFcShort( 0x4 ),	/* 4 */
/* 478 */	NdrFcShort( 0x0 ),	/* 0 */
/* 480 */	NdrFcShort( 0x1 ),	/* 1 */
/* 482 */	NdrFcShort( 0x0 ),	/* 0 */
/* 484 */	NdrFcShort( 0x0 ),	/* 0 */
/* 486 */	0x12, 0x0,	/* FC_UP */
/* 488 */	NdrFcShort( 0xfe2a ),	/* Offset= -470 (18) */
/* 490 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 492 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 494 */	
			0x16,		/* FC_PSTRUCT */
			0x3,		/* 3 */
/* 496 */	NdrFcShort( 0x8 ),	/* 8 */
/* 498 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 500 */	
			0x46,		/* FC_NO_REPEAT */
			0x5c,		/* FC_PAD */
/* 502 */	NdrFcShort( 0x4 ),	/* 4 */
/* 504 */	NdrFcShort( 0x4 ),	/* 4 */
/* 506 */	0x11, 0x0,	/* FC_RP */
/* 508 */	NdrFcShort( 0xffd2 ),	/* Offset= -46 (462) */
/* 510 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 512 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 514 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 516 */	NdrFcShort( 0x0 ),	/* 0 */
/* 518 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 520 */	NdrFcShort( 0x0 ),	/* 0 */
/* 522 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 524 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 528 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 530 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 532 */	NdrFcShort( 0xff4a ),	/* Offset= -182 (350) */
/* 534 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 536 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 538 */	NdrFcShort( 0x8 ),	/* 8 */
/* 540 */	NdrFcShort( 0x0 ),	/* 0 */
/* 542 */	NdrFcShort( 0x6 ),	/* Offset= 6 (548) */
/* 544 */	0x8,		/* FC_LONG */
			0x36,		/* FC_POINTER */
/* 546 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 548 */	
			0x11, 0x0,	/* FC_RP */
/* 550 */	NdrFcShort( 0xffdc ),	/* Offset= -36 (514) */
/* 552 */	
			0x21,		/* FC_BOGUS_ARRAY */
			0x3,		/* 3 */
/* 554 */	NdrFcShort( 0x0 ),	/* 0 */
/* 556 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 558 */	NdrFcShort( 0x0 ),	/* 0 */
/* 560 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 562 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 566 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 568 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 570 */	NdrFcShort( 0xff36 ),	/* Offset= -202 (368) */
/* 572 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 574 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 576 */	NdrFcShort( 0x8 ),	/* 8 */
/* 578 */	NdrFcShort( 0x0 ),	/* 0 */
/* 580 */	NdrFcShort( 0x6 ),	/* Offset= 6 (586) */
/* 582 */	0x8,		/* FC_LONG */
			0x36,		/* FC_POINTER */
/* 584 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 586 */	
			0x11, 0x0,	/* FC_RP */
/* 588 */	NdrFcShort( 0xffdc ),	/* Offset= -36 (552) */
/* 590 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 592 */	NdrFcShort( 0x4 ),	/* 4 */
/* 594 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 596 */	NdrFcShort( 0x0 ),	/* 0 */
/* 598 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 600 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 602 */	
			0x48,		/* FC_VARIABLE_REPEAT */
			0x49,		/* FC_FIXED_OFFSET */
/* 604 */	NdrFcShort( 0x4 ),	/* 4 */
/* 606 */	NdrFcShort( 0x0 ),	/* 0 */
/* 608 */	NdrFcShort( 0x1 ),	/* 1 */
/* 610 */	NdrFcShort( 0x0 ),	/* 0 */
/* 612 */	NdrFcShort( 0x0 ),	/* 0 */
/* 614 */	0x12, 0x0,	/* FC_UP */
/* 616 */	NdrFcShort( 0x192 ),	/* Offset= 402 (1018) */
/* 618 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 620 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 622 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 624 */	NdrFcShort( 0x8 ),	/* 8 */
/* 626 */	NdrFcShort( 0x0 ),	/* 0 */
/* 628 */	NdrFcShort( 0x6 ),	/* Offset= 6 (634) */
/* 630 */	0x8,		/* FC_LONG */
			0x36,		/* FC_POINTER */
/* 632 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 634 */	
			0x11, 0x0,	/* FC_RP */
/* 636 */	NdrFcShort( 0xffd2 ),	/* Offset= -46 (590) */
/* 638 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 640 */	NdrFcLong( 0x2f ),	/* 47 */
/* 644 */	NdrFcShort( 0x0 ),	/* 0 */
/* 646 */	NdrFcShort( 0x0 ),	/* 0 */
/* 648 */	0xc0,		/* 192 */
			0x0,		/* 0 */
/* 650 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 652 */	0x0,		/* 0 */
			0x0,		/* 0 */
/* 654 */	0x0,		/* 0 */
			0x46,		/* 70 */
/* 656 */	
			0x1b,		/* FC_CARRAY */
			0x0,		/* 0 */
/* 658 */	NdrFcShort( 0x1 ),	/* 1 */
/* 660 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 662 */	NdrFcShort( 0x4 ),	/* 4 */
/* 664 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 666 */	0x1,		/* FC_BYTE */
			0x5b,		/* FC_END */
/* 668 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 670 */	NdrFcShort( 0x10 ),	/* 16 */
/* 672 */	NdrFcShort( 0x0 ),	/* 0 */
/* 674 */	NdrFcShort( 0xa ),	/* Offset= 10 (684) */
/* 676 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 678 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 680 */	NdrFcShort( 0xffd6 ),	/* Offset= -42 (638) */
/* 682 */	0x36,		/* FC_POINTER */
			0x5b,		/* FC_END */
/* 684 */	
			0x12, 0x20,	/* FC_UP [maybenull_sizeis] */
/* 686 */	NdrFcShort( 0xffe2 ),	/* Offset= -30 (656) */
/* 688 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 690 */	NdrFcShort( 0x4 ),	/* 4 */
/* 692 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 694 */	NdrFcShort( 0x0 ),	/* 0 */
/* 696 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 698 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 700 */	
			0x48,		/* FC_VARIABLE_REPEAT */
			0x49,		/* FC_FIXED_OFFSET */
/* 702 */	NdrFcShort( 0x4 ),	/* 4 */
/* 704 */	NdrFcShort( 0x0 ),	/* 0 */
/* 706 */	NdrFcShort( 0x1 ),	/* 1 */
/* 708 */	NdrFcShort( 0x0 ),	/* 0 */
/* 710 */	NdrFcShort( 0x0 ),	/* 0 */
/* 712 */	0x12, 0x0,	/* FC_UP */
/* 714 */	NdrFcShort( 0xffd2 ),	/* Offset= -46 (668) */
/* 716 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 718 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 720 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 722 */	NdrFcShort( 0x8 ),	/* 8 */
/* 724 */	NdrFcShort( 0x0 ),	/* 0 */
/* 726 */	NdrFcShort( 0x6 ),	/* Offset= 6 (732) */
/* 728 */	0x8,		/* FC_LONG */
			0x36,		/* FC_POINTER */
/* 730 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 732 */	
			0x11, 0x0,	/* FC_RP */
/* 734 */	NdrFcShort( 0xffd2 ),	/* Offset= -46 (688) */
/* 736 */	
			0x1d,		/* FC_SMFARRAY */
			0x0,		/* 0 */
/* 738 */	NdrFcShort( 0x8 ),	/* 8 */
/* 740 */	0x1,		/* FC_BYTE */
			0x5b,		/* FC_END */
/* 742 */	
			0x15,		/* FC_STRUCT */
			0x3,		/* 3 */
/* 744 */	NdrFcShort( 0x10 ),	/* 16 */
/* 746 */	0x8,		/* FC_LONG */
			0x6,		/* FC_SHORT */
/* 748 */	0x6,		/* FC_SHORT */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 750 */	0x0,		/* 0 */
			NdrFcShort( 0xfff1 ),	/* Offset= -15 (736) */
			0x5b,		/* FC_END */
/* 754 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 756 */	NdrFcShort( 0x18 ),	/* 24 */
/* 758 */	NdrFcShort( 0x0 ),	/* 0 */
/* 760 */	NdrFcShort( 0xa ),	/* Offset= 10 (770) */
/* 762 */	0x8,		/* FC_LONG */
			0x36,		/* FC_POINTER */
/* 764 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 766 */	NdrFcShort( 0xffe8 ),	/* Offset= -24 (742) */
/* 768 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 770 */	
			0x11, 0x0,	/* FC_RP */
/* 772 */	NdrFcShort( 0xfefe ),	/* Offset= -258 (514) */
/* 774 */	
			0x1b,		/* FC_CARRAY */
			0x0,		/* 0 */
/* 776 */	NdrFcShort( 0x1 ),	/* 1 */
/* 778 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 780 */	NdrFcShort( 0x0 ),	/* 0 */
/* 782 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 784 */	0x1,		/* FC_BYTE */
			0x5b,		/* FC_END */
/* 786 */	
			0x16,		/* FC_PSTRUCT */
			0x3,		/* 3 */
/* 788 */	NdrFcShort( 0x8 ),	/* 8 */
/* 790 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 792 */	
			0x46,		/* FC_NO_REPEAT */
			0x5c,		/* FC_PAD */
/* 794 */	NdrFcShort( 0x4 ),	/* 4 */
/* 796 */	NdrFcShort( 0x4 ),	/* 4 */
/* 798 */	0x12, 0x20,	/* FC_UP [maybenull_sizeis] */
/* 800 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (774) */
/* 802 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 804 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 806 */	
			0x1b,		/* FC_CARRAY */
			0x1,		/* 1 */
/* 808 */	NdrFcShort( 0x2 ),	/* 2 */
/* 810 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 812 */	NdrFcShort( 0x0 ),	/* 0 */
/* 814 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 816 */	0x6,		/* FC_SHORT */
			0x5b,		/* FC_END */
/* 818 */	
			0x16,		/* FC_PSTRUCT */
			0x3,		/* 3 */
/* 820 */	NdrFcShort( 0x8 ),	/* 8 */
/* 822 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 824 */	
			0x46,		/* FC_NO_REPEAT */
			0x5c,		/* FC_PAD */
/* 826 */	NdrFcShort( 0x4 ),	/* 4 */
/* 828 */	NdrFcShort( 0x4 ),	/* 4 */
/* 830 */	0x12, 0x20,	/* FC_UP [maybenull_sizeis] */
/* 832 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (806) */
/* 834 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 836 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 838 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 840 */	NdrFcShort( 0x4 ),	/* 4 */
/* 842 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 844 */	NdrFcShort( 0x0 ),	/* 0 */
/* 846 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 848 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 850 */	
			0x16,		/* FC_PSTRUCT */
			0x3,		/* 3 */
/* 852 */	NdrFcShort( 0x8 ),	/* 8 */
/* 854 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 856 */	
			0x46,		/* FC_NO_REPEAT */
			0x5c,		/* FC_PAD */
/* 858 */	NdrFcShort( 0x4 ),	/* 4 */
/* 860 */	NdrFcShort( 0x4 ),	/* 4 */
/* 862 */	0x12, 0x20,	/* FC_UP [maybenull_sizeis] */
/* 864 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (838) */
/* 866 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 868 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 870 */	
			0x1b,		/* FC_CARRAY */
			0x7,		/* 7 */
/* 872 */	NdrFcShort( 0x8 ),	/* 8 */
/* 874 */	0x19,		/* Corr desc:  field pointer, FC_ULONG */
			0x0,		/*  */
/* 876 */	NdrFcShort( 0x0 ),	/* 0 */
/* 878 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 880 */	0xb,		/* FC_HYPER */
			0x5b,		/* FC_END */
/* 882 */	
			0x16,		/* FC_PSTRUCT */
			0x3,		/* 3 */
/* 884 */	NdrFcShort( 0x8 ),	/* 8 */
/* 886 */	
			0x4b,		/* FC_PP */
			0x5c,		/* FC_PAD */
/* 888 */	
			0x46,		/* FC_NO_REPEAT */
			0x5c,		/* FC_PAD */
/* 890 */	NdrFcShort( 0x4 ),	/* 4 */
/* 892 */	NdrFcShort( 0x4 ),	/* 4 */
/* 894 */	0x12, 0x20,	/* FC_UP [maybenull_sizeis] */
/* 896 */	NdrFcShort( 0xffe6 ),	/* Offset= -26 (870) */
/* 898 */	
			0x5b,		/* FC_END */

			0x8,		/* FC_LONG */
/* 900 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */
/* 902 */	
			0x15,		/* FC_STRUCT */
			0x3,		/* 3 */
/* 904 */	NdrFcShort( 0x8 ),	/* 8 */
/* 906 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 908 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 910 */	
			0x1b,		/* FC_CARRAY */
			0x3,		/* 3 */
/* 912 */	NdrFcShort( 0x8 ),	/* 8 */
/* 914 */	0x7,		/* Corr desc: FC_USHORT */
			0x0,		/*  */
/* 916 */	NdrFcShort( 0xffd8 ),	/* -40 */
/* 918 */	NdrFcShort( 0x1 ),	/* Corr flags:  early, */
/* 920 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 922 */	NdrFcShort( 0xffec ),	/* Offset= -20 (902) */
/* 924 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 926 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x3,		/* 3 */
/* 928 */	NdrFcShort( 0x28 ),	/* 40 */
/* 930 */	NdrFcShort( 0xffec ),	/* Offset= -20 (910) */
/* 932 */	NdrFcShort( 0x0 ),	/* Offset= 0 (932) */
/* 934 */	0x6,		/* FC_SHORT */
			0x6,		/* FC_SHORT */
/* 936 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 938 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 940 */	NdrFcShort( 0xfdde ),	/* Offset= -546 (394) */
/* 942 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 944 */	
			0x12, 0x0,	/* FC_UP */
/* 946 */	NdrFcShort( 0xfeea ),	/* Offset= -278 (668) */
/* 948 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 950 */	0x1,		/* FC_BYTE */
			0x5c,		/* FC_PAD */
/* 952 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 954 */	0x6,		/* FC_SHORT */
			0x5c,		/* FC_PAD */
/* 956 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 958 */	0x8,		/* FC_LONG */
			0x5c,		/* FC_PAD */
/* 960 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 962 */	0xb,		/* FC_HYPER */
			0x5c,		/* FC_PAD */
/* 964 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 966 */	0xa,		/* FC_FLOAT */
			0x5c,		/* FC_PAD */
/* 968 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 970 */	0xc,		/* FC_DOUBLE */
			0x5c,		/* FC_PAD */
/* 972 */	
			0x12, 0x0,	/* FC_UP */
/* 974 */	NdrFcShort( 0xfd8a ),	/* Offset= -630 (344) */
/* 976 */	
			0x12, 0x10,	/* FC_UP [pointer_deref] */
/* 978 */	NdrFcShort( 0xfc30 ),	/* Offset= -976 (2) */
/* 980 */	
			0x12, 0x10,	/* FC_UP [pointer_deref] */
/* 982 */	NdrFcShort( 0xfd88 ),	/* Offset= -632 (350) */
/* 984 */	
			0x12, 0x10,	/* FC_UP [pointer_deref] */
/* 986 */	NdrFcShort( 0xfd96 ),	/* Offset= -618 (368) */
/* 988 */	
			0x12, 0x10,	/* FC_UP [pointer_deref] */
/* 990 */	NdrFcShort( 0xfda4 ),	/* Offset= -604 (386) */
/* 992 */	
			0x12, 0x10,	/* FC_UP [pointer_deref] */
/* 994 */	NdrFcShort( 0x2 ),	/* Offset= 2 (996) */
/* 996 */	
			0x12, 0x0,	/* FC_UP */
/* 998 */	NdrFcShort( 0x14 ),	/* Offset= 20 (1018) */
/* 1000 */	
			0x15,		/* FC_STRUCT */
			0x7,		/* 7 */
/* 1002 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1004 */	0x6,		/* FC_SHORT */
			0x1,		/* FC_BYTE */
/* 1006 */	0x1,		/* FC_BYTE */
			0x8,		/* FC_LONG */
/* 1008 */	0xb,		/* FC_HYPER */
			0x5b,		/* FC_END */
/* 1010 */	
			0x12, 0x0,	/* FC_UP */
/* 1012 */	NdrFcShort( 0xfff4 ),	/* Offset= -12 (1000) */
/* 1014 */	
			0x12, 0x8,	/* FC_UP [simple_pointer] */
/* 1016 */	0x2,		/* FC_CHAR */
			0x5c,		/* FC_PAD */
/* 1018 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x7,		/* 7 */
/* 1020 */	NdrFcShort( 0x20 ),	/* 32 */
/* 1022 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1024 */	NdrFcShort( 0x0 ),	/* Offset= 0 (1024) */
/* 1026 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 1028 */	0x6,		/* FC_SHORT */
			0x6,		/* FC_SHORT */
/* 1030 */	0x6,		/* FC_SHORT */
			0x6,		/* FC_SHORT */
/* 1032 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 1034 */	NdrFcShort( 0xfc24 ),	/* Offset= -988 (46) */
/* 1036 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 1038 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 1040 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1042 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1044 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1046 */	NdrFcShort( 0xfc14 ),	/* Offset= -1004 (42) */
/* 1048 */	
			0x11, 0xc,	/* FC_RP [alloced_on_stack] [simple_pointer] */
/* 1050 */	0x8,		/* FC_LONG */
			0x5c,		/* FC_PAD */
/* 1052 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1054 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1056) */
/* 1056 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1058 */	NdrFcLong( 0xf51af265 ),	/* -182783387 */
/* 1062 */	NdrFcShort( 0xadb1 ),	/* -21071 */
/* 1064 */	NdrFcShort( 0x40b7 ),	/* 16567 */
/* 1066 */	0xb2,		/* 178 */
			0xcb,		/* 203 */
/* 1068 */	0xa2,		/* 162 */
			0x58,		/* 88 */
/* 1070 */	0xfd,		/* 253 */
			0xe6,		/* 230 */
/* 1072 */	0x90,		/* 144 */
			0x1e,		/* 30 */
/* 1074 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 1076 */	NdrFcShort( 0x6 ),	/* Offset= 6 (1082) */
/* 1078 */	
			0x13, 0x0,	/* FC_OP */
/* 1080 */	NdrFcShort( 0xfbda ),	/* Offset= -1062 (18) */
/* 1082 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 1084 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1086 */	NdrFcShort( 0x4 ),	/* 4 */
/* 1088 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1090 */	NdrFcShort( 0xfff4 ),	/* Offset= -12 (1078) */
/* 1092 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1094 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1096) */
/* 1096 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1098 */	NdrFcLong( 0xc51ebe57 ),	/* -987840937 */
/* 1102 */	NdrFcShort( 0x76b2 ),	/* 30386 */
/* 1104 */	NdrFcShort( 0x4c8c ),	/* 19596 */
/* 1106 */	0xa4,		/* 164 */
			0x4b,		/* 75 */
/* 1108 */	0x13,		/* 19 */
			0xe3,		/* 227 */
/* 1110 */	0x4f,		/* 79 */
			0x84,		/* 132 */
/* 1112 */	0x6f,		/* 111 */
			0x2e,		/* 46 */
/* 1114 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1116 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1118) */
/* 1118 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1120 */	NdrFcLong( 0x77cd7e2f ),	/* 2009955887 */
/* 1124 */	NdrFcShort( 0x5b62 ),	/* 23394 */
/* 1126 */	NdrFcShort( 0x451d ),	/* 17693 */
/* 1128 */	0xab,		/* 171 */
			0x3d,		/* 61 */
/* 1130 */	0x81,		/* 129 */
			0x2f,		/* 47 */
/* 1132 */	0x8b,		/* 139 */
			0x9f,		/* 159 */
/* 1134 */	0xcb,		/* 203 */
			0xa8,		/* 168 */
/* 1136 */	
			0x11, 0xc,	/* FC_RP [alloced_on_stack] [simple_pointer] */
/* 1138 */	0x6,		/* FC_SHORT */
			0x5c,		/* FC_PAD */
/* 1140 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1142 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1144) */
/* 1144 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1146 */	NdrFcLong( 0x5da5c3ee ),	/* 1571144686 */
/* 1150 */	NdrFcShort( 0xe167 ),	/* -7833 */
/* 1152 */	NdrFcShort( 0x4e57 ),	/* 20055 */
/* 1154 */	0xa8,		/* 168 */
			0xe9,		/* 233 */
/* 1156 */	0x22,		/* 34 */
			0xef,		/* 239 */
/* 1158 */	0x1c,		/* 28 */
			0x8b,		/* 139 */
/* 1160 */	0x45,		/* 69 */
			0xc4,		/* 196 */
/* 1162 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 1164 */	NdrFcShort( 0x6 ),	/* Offset= 6 (1170) */
/* 1166 */	
			0x13, 0x0,	/* FC_OP */
/* 1168 */	NdrFcShort( 0xff6a ),	/* Offset= -150 (1018) */
/* 1170 */	0xb4,		/* FC_USER_MARSHAL */
			0x83,		/* 131 */
/* 1172 */	NdrFcShort( 0x1 ),	/* 1 */
/* 1174 */	NdrFcShort( 0x10 ),	/* 16 */
/* 1176 */	NdrFcShort( 0x0 ),	/* 0 */
/* 1178 */	NdrFcShort( 0xfff4 ),	/* Offset= -12 (1166) */
/* 1180 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1182 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1184) */
/* 1184 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1186 */	NdrFcLong( 0x99dbfa2f ),	/* -1713636817 */
/* 1190 */	NdrFcShort( 0x9699 ),	/* -26983 */
/* 1192 */	NdrFcShort( 0x40eb ),	/* 16619 */
/* 1194 */	0xac,		/* 172 */
			0xee,		/* 238 */
/* 1196 */	0xb8,		/* 184 */
			0x90,		/* 144 */
/* 1198 */	0x90,		/* 144 */
			0x8a,		/* 138 */
/* 1200 */	0xa5,		/* 165 */
			0x83,		/* 131 */
/* 1202 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1204 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1206) */
/* 1206 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1208 */	NdrFcLong( 0x8cd33727 ),	/* -1932314841 */
/* 1212 */	NdrFcShort( 0xa92 ),	/* 2706 */
/* 1214 */	NdrFcShort( 0x4560 ),	/* 17760 */
/* 1216 */	0x80,		/* 128 */
			0xd7,		/* 215 */
/* 1218 */	0x1a,		/* 26 */
			0x44,		/* 68 */
/* 1220 */	0x65,		/* 101 */
			0x12,		/* 18 */
/* 1222 */	0xe,		/* 14 */
			0x9b,		/* 155 */
/* 1224 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1226 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1228) */
/* 1228 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1230 */	NdrFcLong( 0x2b527ff9 ),	/* 726827001 */
/* 1234 */	NdrFcShort( 0xdeac ),	/* -8532 */
/* 1236 */	NdrFcShort( 0x481a ),	/* 18458 */
/* 1238 */	0x9f,		/* 159 */
			0x2c,		/* 44 */
/* 1240 */	0x3e,		/* 62 */
			0x7b,		/* 123 */
/* 1242 */	0x27,		/* 39 */
			0xad,		/* 173 */
/* 1244 */	0xce,		/* 206 */
			0xd8,		/* 216 */
/* 1246 */	
			0x11, 0xc,	/* FC_RP [alloced_on_stack] [simple_pointer] */
/* 1248 */	0xc,		/* FC_DOUBLE */
			0x5c,		/* FC_PAD */
/* 1250 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1252 */	NdrFcShort( 0xfc7a ),	/* Offset= -902 (350) */
/* 1254 */	
			0x11, 0x10,	/* FC_RP [pointer_deref] */
/* 1256 */	NdrFcShort( 0x2 ),	/* Offset= 2 (1258) */
/* 1258 */	
			0x2f,		/* FC_IP */
			0x5a,		/* FC_CONSTANT_IID */
/* 1260 */	NdrFcLong( 0x15711c8c ),	/* 359734412 */
/* 1264 */	NdrFcShort( 0x52b3 ),	/* 21171 */
/* 1266 */	NdrFcShort( 0x4a0b ),	/* 18955 */
/* 1268 */	0x90,		/* 144 */
			0xb0,		/* 176 */
/* 1270 */	0x12,		/* 18 */
			0x3c,		/* 60 */
/* 1272 */	0x8a,		/* 138 */
			0xd,		/* 13 */
/* 1274 */	0x52,		/* 82 */
			0x5b,		/* 91 */

			0x0
        }
    };

XFG_TRAMPOLINES(BSTR)
XFG_TRAMPOLINES(VARIANT)

static const USER_MARSHAL_ROUTINE_QUADRUPLE UserMarshalRoutines[ WIRE_MARSHAL_TABLE_SIZE ] = 
        {
            
            {
            (USER_MARSHAL_SIZING_ROUTINE)XFG_TRAMPOLINE_FPTR(BSTR_UserSize)
            ,(USER_MARSHAL_MARSHALLING_ROUTINE)XFG_TRAMPOLINE_FPTR(BSTR_UserMarshal)
            ,(USER_MARSHAL_UNMARSHALLING_ROUTINE)XFG_TRAMPOLINE_FPTR(BSTR_UserUnmarshal)
            ,(USER_MARSHAL_FREEING_ROUTINE)XFG_TRAMPOLINE_FPTR(BSTR_UserFree)
            
            }
            ,
            {
            (USER_MARSHAL_SIZING_ROUTINE)XFG_TRAMPOLINE_FPTR(VARIANT_UserSize)
            ,(USER_MARSHAL_MARSHALLING_ROUTINE)XFG_TRAMPOLINE_FPTR(VARIANT_UserMarshal)
            ,(USER_MARSHAL_UNMARSHALLING_ROUTINE)XFG_TRAMPOLINE_FPTR(VARIANT_UserUnmarshal)
            ,(USER_MARSHAL_FREEING_ROUTINE)XFG_TRAMPOLINE_FPTR(VARIANT_UserFree)
            
            }
            

        };



/* Standard interface: __MIDL_itf_salamander_0000_0000, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IUnknown, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: ISalamander, ver. 0.0,
   GUID={0x60F57ABE,0x95C7,0x48c7,{0xAE,0x37,0x9D,0xA0,0x07,0xB8,0xD5,0x7F}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamander_FormatStringOffsetTable[] =
    {
    0,
    54,
    90,
    126,
    162,
    198,
    234,
    288,
    324,
    360,
    396,
    426,
    462,
    498,
    534,
    570,
    612,
    660,
    690,
    750,
    810,
    864,
    900,
    942,
    984,
    1020
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamander_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamander_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamander_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamander_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(29) _ISalamanderProxyVtbl = 
{
    &ISalamander_ProxyInfo,
    &IID_ISalamander,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamander::MsgBox */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_LeftPanel */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_RightPanel */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_SourcePanel */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_TargetPanel */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_Version */ ,
    (void *) (INT_PTR) -1 /* ISalamander::InputBox */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_WindowsVersion */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_AutomationVersion */ ,
    (void *) (INT_PTR) -1 /* ISalamander::Sleep */ ,
    (void *) (INT_PTR) -1 /* ISalamander::AbortScript */ ,
    (void *) (INT_PTR) -1 /* ISalamander::TraceI */ ,
    (void *) (INT_PTR) -1 /* ISalamander::TraceE */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_ProgressDialog */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_WaitWindow */ ,
    (void *) (INT_PTR) -1 /* ISalamander::ViewFile */ ,
    (void *) (INT_PTR) -1 /* ISalamander::MatchesMask */ ,
    (void *) (INT_PTR) -1 /* ISalamander::DebugBreak */ ,
    (void *) (INT_PTR) -1 /* ISalamander::ErrorDialog */ ,
    (void *) (INT_PTR) -1 /* ISalamander::QuestionDialog */ ,
    (void *) (INT_PTR) -1 /* ISalamander::OverwriteDialog */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_Forms */ ,
    (void *) (INT_PTR) -1 /* ISalamander::SetPersistentVal */ ,
    (void *) (INT_PTR) -1 /* ISalamander::GetPersistentVal */ ,
    (void *) (INT_PTR) -1 /* ISalamander::get_Script */ ,
    (void *) (INT_PTR) -1 /* ISalamander::GetFullPath */
};

const CInterfaceStubVtbl _ISalamanderStubVtbl =
{
    &IID_ISalamander,
    &ISalamander_ServerInfo,
    29,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderPanel, ver. 0.0,
   GUID={0xF51AF265,0xADB1,0x40b7,{0xB2,0xCB,0xA2,0x58,0xFD,0xE6,0x90,0x1E}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderPanel_FormatStringOffsetTable[] =
    {
    1068,
    1104,
    1140,
    1176,
    1212,
    1248,
    1278,
    288,
    1314,
    (unsigned short) -1
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderPanel_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderPanel_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderPanel_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderPanel_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(13) _ISalamanderPanelProxyVtbl = 
{
    &ISalamanderPanel_ProxyInfo,
    &IID_ISalamanderPanel,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::put_Path */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::get_Path */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::get_FocusedItem */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::get_SelectedItems */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::get_Items */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::SelectAll */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::DeselectAll */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::get_PathType */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanel::StoreSelection */ ,
    0 /* ISalamanderPanel::_GetPanelIndex */
};

const CInterfaceStubVtbl _ISalamanderPanelStubVtbl =
{
    &IID_ISalamanderPanel,
    &ISalamanderPanel_ServerInfo,
    13,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderPanelItem, ver. 0.0,
   GUID={0x8CD33727,0x0A92,0x4560,{0x80,0xD7,0x1A,0x44,0x65,0x12,0x0E,0x9B}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderPanelItem_FormatStringOffsetTable[] =
    {
    1344,
    1104,
    1380,
    1416,
    1452
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderPanelItem_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderPanelItem_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderPanelItem_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderPanelItem_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(8) _ISalamanderPanelItemProxyVtbl = 
{
    &ISalamanderPanelItem_ProxyInfo,
    &IID_ISalamanderPanelItem,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItem::get_Name */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItem::get_Path */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItem::get_Size */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItem::get_DateLastModified */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItem::get_Attributes */
};

const CInterfaceStubVtbl _ISalamanderPanelItemStubVtbl =
{
    &IID_ISalamanderPanelItem,
    &ISalamanderPanelItem_ServerInfo,
    8,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderPanelItemCollection, ver. 0.0,
   GUID={0x2B527FF9,0xDEAC,0x481a,{0x9F,0x2C,0x3E,0x7B,0x27,0xAD,0xCE,0xD8}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderPanelItemCollection_FormatStringOffsetTable[] =
    {
    1488,
    1530,
    1566
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderPanelItemCollection_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderPanelItemCollection_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderPanelItemCollection_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderPanelItemCollection_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(6) _ISalamanderPanelItemCollectionProxyVtbl = 
{
    &ISalamanderPanelItemCollection_ProxyInfo,
    &IID_ISalamanderPanelItemCollection,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItemCollection::get_Item */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItemCollection::get_Count */ ,
    (void *) (INT_PTR) -1 /* ISalamanderPanelItemCollection::get__NewEnum */
};

const CInterfaceStubVtbl _ISalamanderPanelItemCollectionStubVtbl =
{
    &IID_ISalamanderPanelItemCollection,
    &ISalamanderPanelItemCollection_ServerInfo,
    6,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderProgressDialog, ver. 0.0,
   GUID={0xC51EBE57,0x76B2,0x4c8c,{0xA4,0x4B,0x13,0xE3,0x4F,0x84,0x6F,0x2E}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderProgressDialog_FormatStringOffsetTable[] =
    {
    1602,
    1632,
    1662,
    1698,
    1734,
    1770,
    1806,
    1842,
    1878,
    1914,
    1950,
    1986,
    2022,
    2058,
    2094,
    2130,
    2166,
    2202,
    2238
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderProgressDialog_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderProgressDialog_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderProgressDialog_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderProgressDialog_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(22) _ISalamanderProgressDialogProxyVtbl = 
{
    &ISalamanderProgressDialog_ProxyInfo,
    &IID_ISalamanderProgressDialog,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::Show */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::Hide */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_Title */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::put_Title */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::AddText */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_IsCancelled */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_Position */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::put_Position */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_TotalPosition */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::put_TotalPosition */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::Step */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_CanCancel */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::put_CanCancel */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_Style */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::put_Style */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_Maximum */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::put_Maximum */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::get_TotalMaximum */ ,
    (void *) (INT_PTR) -1 /* ISalamanderProgressDialog::put_TotalMaximum */
};

const CInterfaceStubVtbl _ISalamanderProgressDialogStubVtbl =
{
    &IID_ISalamanderProgressDialog,
    &ISalamanderProgressDialog_ServerInfo,
    22,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderWaitWindow, ver. 0.0,
   GUID={0x77CD7E2F,0x5B62,0x451d,{0xAB,0x3D,0x81,0x2F,0x8B,0x9F,0xCB,0xA8}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderWaitWindow_FormatStringOffsetTable[] =
    {
    1602,
    1632,
    1662,
    1698,
    2274,
    2310,
    2346,
    288,
    2382,
    2418,
    2454
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderWaitWindow_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderWaitWindow_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderWaitWindow_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderWaitWindow_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(14) _ISalamanderWaitWindowProxyVtbl = 
{
    &ISalamanderWaitWindow_ProxyInfo,
    &IID_ISalamanderWaitWindow,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::Show */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::Hide */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::get_Title */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::put_Title */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::get_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::put_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::get_IsCancelled */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::get_Delay */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::put_Delay */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::get_CanCancel */ ,
    (void *) (INT_PTR) -1 /* ISalamanderWaitWindow::put_CanCancel */
};

const CInterfaceStubVtbl _ISalamanderWaitWindowStubVtbl =
{
    &IID_ISalamanderWaitWindow,
    &ISalamanderWaitWindow_ServerInfo,
    14,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderScriptInfo, ver. 0.0,
   GUID={0x99DBFA2F,0x9699,0x40eb,{0xAC,0xEE,0xB8,0x90,0x90,0x8A,0xA5,0x83}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderScriptInfo_FormatStringOffsetTable[] =
    {
    1344,
    1104
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderScriptInfo_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderScriptInfo_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderScriptInfo_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderScriptInfo_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(5) _ISalamanderScriptInfoProxyVtbl = 
{
    &ISalamanderScriptInfo_ProxyInfo,
    &IID_ISalamanderScriptInfo,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderScriptInfo::get_Name */ ,
    (void *) (INT_PTR) -1 /* ISalamanderScriptInfo::get_Path */
};

const CInterfaceStubVtbl _ISalamanderScriptInfoStubVtbl =
{
    &IID_ISalamanderScriptInfo,
    &ISalamanderScriptInfo_ServerInfo,
    5,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderGuiComponent, ver. 0.0,
   GUID={0x15711C8C,0x52B3,0x4a0b,{0x90,0xB0,0x12,0x3C,0x8A,0x0D,0x52,0x5B}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderGuiComponent_FormatStringOffsetTable[] =
    {
    1344,
    2490
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiComponent_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiComponent_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderGuiComponent_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiComponent_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(5) _ISalamanderGuiComponentProxyVtbl = 
{
    &ISalamanderGuiComponent_ProxyInfo,
    &IID_ISalamanderGuiComponent,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::get_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::put_Text */
};

const CInterfaceStubVtbl _ISalamanderGuiComponentStubVtbl =
{
    &IID_ISalamanderGuiComponent,
    &ISalamanderGuiComponent_ServerInfo,
    5,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Standard interface: __MIDL_itf_salamander_0000_0008, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: ISalamanderGuiComponentInternal, ver. 0.0,
   GUID={0x037905FB,0xA081,0x4a60,{0xAE,0x61,0xC8,0x48,0x17,0x7F,0x99,0xE0}} */


/* Object interface: ISalamanderGuiCheckBox, ver. 0.0,
   GUID={0xA26789BB,0xCAA4,0x47d5,{0xBC,0xC5,0x38,0x6E,0xF2,0x42,0x20,0xD4}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderGuiCheckBox_FormatStringOffsetTable[] =
    {
    1344,
    2490,
    2526,
    2562
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiCheckBox_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiCheckBox_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderGuiCheckBox_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiCheckBox_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(7) _ISalamanderGuiCheckBoxProxyVtbl = 
{
    &ISalamanderGuiCheckBox_ProxyInfo,
    &IID_ISalamanderGuiCheckBox,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::get_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::put_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiCheckBox::get_Checked */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiCheckBox::put_Checked */
};

const CInterfaceStubVtbl _ISalamanderGuiCheckBoxStubVtbl =
{
    &IID_ISalamanderGuiCheckBox,
    &ISalamanderGuiCheckBox_ServerInfo,
    7,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderGuiButton, ver. 0.0,
   GUID={0xF30E467C,0x0FEA,0x4e67,{0x9F,0x51,0xC2,0xB1,0xA2,0xA3,0x0D,0x4E}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderGuiButton_FormatStringOffsetTable[] =
    {
    1344,
    2490,
    2598,
    2634
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiButton_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiButton_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderGuiButton_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiButton_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(7) _ISalamanderGuiButtonProxyVtbl = 
{
    &ISalamanderGuiButton_ProxyInfo,
    &IID_ISalamanderGuiButton,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::get_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::put_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiButton::get_DialogResult */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiButton::put_DialogResult */
};

const CInterfaceStubVtbl _ISalamanderGuiButtonStubVtbl =
{
    &IID_ISalamanderGuiButton,
    &ISalamanderGuiButton_ServerInfo,
    7,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderGuiContainer, ver. 0.0,
   GUID={0x6A31B232,0xB6AF,0x467a,{0xB8,0x18,0x21,0x2C,0xCC,0x6F,0x4C,0x5D}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderGuiContainer_FormatStringOffsetTable[] =
    {
    1344,
    2490,
    0
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiContainer_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiContainer_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderGuiContainer_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiContainer_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(5) _ISalamanderGuiContainerProxyVtbl = 
{
    0,
    &IID_ISalamanderGuiContainer,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    0 /* forced delegation ISalamanderGuiComponent::get_Text */ ,
    0 /* forced delegation ISalamanderGuiComponent::put_Text */
};


EXTERN_C DECLSPEC_SELECTANY const PRPC_STUB_FUNCTION ISalamanderGuiContainer_table[] =
{
    NdrStubCall2,
    NdrStubCall2
};

CInterfaceStubVtbl _ISalamanderGuiContainerStubVtbl =
{
    &IID_ISalamanderGuiContainer,
    &ISalamanderGuiContainer_ServerInfo,
    5,
    &ISalamanderGuiContainer_table[-3],
    CStdStubBuffer_DELEGATING_METHODS
};


/* Object interface: ISalamanderGuiForm, ver. 0.0,
   GUID={0x7356DD12,0xA7DA,0x41EB,{0x8E,0x31,0xF4,0x2C,0x46,0x5C,0x87,0xE1}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderGuiForm_FormatStringOffsetTable[] =
    {
    1344,
    2490,
    2598
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderGuiForm_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiForm_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderGuiForm_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGuiForm_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(6) _ISalamanderGuiFormProxyVtbl = 
{
    &ISalamanderGuiForm_ProxyInfo,
    &IID_ISalamanderGuiForm,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::get_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiComponent::put_Text */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGuiForm::Execute */
};

const CInterfaceStubVtbl _ISalamanderGuiFormStubVtbl =
{
    &IID_ISalamanderGuiForm,
    &ISalamanderGuiForm_ServerInfo,
    6,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: ISalamanderGui, ver. 0.0,
   GUID={0x5DA5C3EE,0xE167,0x4E57,{0xA8,0xE9,0x22,0xEF,0x1C,0x8B,0x45,0xC4}} */

#pragma code_seg(".orpc")
static const unsigned short ISalamanderGui_FormatStringOffsetTable[] =
    {
    2670,
    2712,
    2760,
    2802,
    2844
    };

static const MIDL_STUBLESS_PROXY_INFO ISalamanderGui_ProxyInfo =
    {
    &Object_StubDesc,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGui_FormatStringOffsetTable[-3],
    0,
    0,
    0
    };


static const MIDL_SERVER_INFO ISalamanderGui_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    salamander__MIDL_ProcFormatString.Format,
    &ISalamanderGui_FormatStringOffsetTable[-3],
    0,
    0,
    0,
    0};
CINTERFACE_PROXY_VTABLE(8) _ISalamanderGuiProxyVtbl = 
{
    &ISalamanderGui_ProxyInfo,
    &IID_ISalamanderGui,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    (void *) (INT_PTR) -1 /* ISalamanderGui::Form */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGui::Button */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGui::CheckBox */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGui::TextBox */ ,
    (void *) (INT_PTR) -1 /* ISalamanderGui::Label */
};

const CInterfaceStubVtbl _ISalamanderGuiStubVtbl =
{
    &IID_ISalamanderGui,
    &ISalamanderGui_ServerInfo,
    8,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};

#ifdef __cplusplus
namespace {
#endif
static const MIDL_STUB_DESC Object_StubDesc = 
    {
    0,
    NdrOleAllocate,
    NdrOleFree,
    0,
    0,
    0,
    0,
    0,
    salamander__MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0x60001, /* Ndr library version */
    0,
    0x8010274, /* MIDL Version 8.1.628 */
    0,
    UserMarshalRoutines,
    0,  /* notify & notify_flag routine table */
    0x1, /* MIDL flag */
    0, /* cs routines */
    0,   /* proxy/server info */
    0
    };
#ifdef __cplusplus
}
#endif

const CInterfaceProxyVtbl * const _salamander_ProxyVtblList[] = 
{
    ( CInterfaceProxyVtbl *) &_ISalamanderGuiFormProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderPanelItemProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderWaitWindowProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderScriptInfoProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderGuiContainerProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderProgressDialogProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderPanelProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderGuiButtonProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderGuiComponentProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderGuiCheckBoxProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderGuiProxyVtbl,
    ( CInterfaceProxyVtbl *) &_ISalamanderPanelItemCollectionProxyVtbl,
    0
};

const CInterfaceStubVtbl * const _salamander_StubVtblList[] = 
{
    ( CInterfaceStubVtbl *) &_ISalamanderGuiFormStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderPanelItemStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderWaitWindowStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderScriptInfoStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderGuiContainerStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderProgressDialogStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderPanelStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderGuiButtonStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderGuiComponentStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderGuiCheckBoxStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderGuiStubVtbl,
    ( CInterfaceStubVtbl *) &_ISalamanderPanelItemCollectionStubVtbl,
    0
};

PCInterfaceName const _salamander_InterfaceNamesList[] = 
{
    "ISalamanderGuiForm",
    "ISalamanderPanelItem",
    "ISalamanderWaitWindow",
    "ISalamanderScriptInfo",
    "ISalamanderGuiContainer",
    "ISalamanderProgressDialog",
    "ISalamanderPanel",
    "ISalamanderGuiButton",
    "ISalamanderGuiComponent",
    "ISalamanderGuiCheckBox",
    "ISalamander",
    "ISalamanderGui",
    "ISalamanderPanelItemCollection",
    0
};

const IID *  const _salamander_BaseIIDList[] = 
{
    0,
    0,
    0,
    0,
    &IID_ISalamanderGuiComponent,   /* forced */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0
};


#define _salamander_CHECK_IID(n)	IID_GENERIC_CHECK_IID( _salamander, pIID, n)

int __stdcall _salamander_IID_Lookup( const IID * pIID, int * pIndex )
{
    IID_BS_LOOKUP_SETUP

    IID_BS_LOOKUP_INITIAL_TEST( _salamander, 13, 8 )
    IID_BS_LOOKUP_NEXT_TEST( _salamander, 4 )
    IID_BS_LOOKUP_NEXT_TEST( _salamander, 2 )
    IID_BS_LOOKUP_NEXT_TEST( _salamander, 1 )
    IID_BS_LOOKUP_RETURN_RESULT( _salamander, 13, *pIndex )
    
}

EXTERN_C const ExtendedProxyFileInfo salamander_ProxyFileInfo = 
{
    (PCInterfaceProxyVtblList *) & _salamander_ProxyVtblList,
    (PCInterfaceStubVtblList *) & _salamander_StubVtblList,
    (const PCInterfaceName * ) & _salamander_InterfaceNamesList,
    (const IID ** ) & _salamander_BaseIIDList,
    & _salamander_IID_Lookup, 
    13,
    2,
    0, /* table of [async_uuid] interfaces */
    0, /* Filler1 */
    0, /* Filler2 */
    0  /* Filler3 */
};
#pragma optimize("", on )
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* !defined(_M_IA64) && !defined(_M_AMD64) && !defined(_ARM_) */

