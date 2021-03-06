/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apxwin.h"
#include "private.h"

#include <jni.h>

#ifndef JNI_VERSION_1_2
#error -------------------------------------------------------
#error JAVA 1.1 IS NO LONGER SUPPORTED
#error -------------------------------------------------------
#endif

#ifdef JNI_VERSION_1_4
#define JNI_VERSION_DEFAULT JNI_VERSION_1_4
#else
#define JNI_VERSION_DEFAULT JNI_VERSION_1_2
#endif

/* Standard jvm.dll prototypes
 * since only single jvm can exist per process
 * make those global
 */

DYNOLAD_TYPE_DECLARE(JNI_GetDefaultJavaVMInitArgs, JNICALL, jint)(void *);
static DYNLOAD_FPTR_DECLARE(JNI_GetDefaultJavaVMInitArgs) = NULL;

DYNOLAD_TYPE_DECLARE(JNI_CreateJavaVM, JNICALL, jint)(JavaVM **, void **, void *);
static DYNLOAD_FPTR_DECLARE(JNI_CreateJavaVM) = NULL;

DYNOLAD_TYPE_DECLARE(JNI_GetCreatedJavaVMs, JNICALL, jint)(JavaVM **, jsize, jsize *);
static DYNLOAD_FPTR_DECLARE(JNI_GetCreatedJavaVMs) = NULL;

static HANDLE _st_sys_jvmDllHandle = NULL;

DYNOLAD_TYPE_DECLARE(SetDllDirectoryW, WINAPI, BOOL)(LPCWSTR);
static DYNLOAD_FPTR_DECLARE(SetDllDirectoryW) = NULL;

#define JVM_DELETE_CLAZZ(jvm, cl)                                               \
    APXMACRO_BEGIN                                                              \
    if ((jvm)->lpEnv && (jvm)->##cl.jClazz) {                                   \
        (*((jvm)->lpEnv))->DeleteGlobalRef((jvm)->lpEnv, (jvm)->##cl.jClazz);   \
        (jvm)->##cl.jClazz = NULL;                                              \
    } APXMACRO_END

#define JVM_EXCEPTION_CHECK(jvm) \
    ((*((jvm)->lpEnv))->ExceptionCheck((jvm)->lpEnv) != JNI_OK)

#ifdef _DEBUG
#define JVM_EXCEPTION_CLEAR(jvm) \
    APXMACRO_BEGIN                                              \
    if ((jvm)->lpEnv) {                                         \
        if ((*((jvm)->lpEnv))->ExceptionCheck((jvm)->lpEnv)) {  \
            (*((jvm)->lpEnv))->ExceptionDescribe((jvm)->lpEnv); \
            (*((jvm)->lpEnv))->ExceptionClear((jvm)->lpEnv);    \
        }                                                       \
    } APXMACRO_END
#else
#define JVM_EXCEPTION_CLEAR(jvm) \
    APXMACRO_BEGIN                                              \
    if ((jvm)->lpEnv) {                                         \
        if ((*((jvm)->lpEnv))->ExceptionCheck((jvm)->lpEnv)) {  \
            (*((jvm)->lpEnv))->ExceptionClear((jvm)->lpEnv);    \
        }                                                       \
    } APXMACRO_END
#endif

#define JNI_LOCAL_UNREF(obj) \
        (*(lpJava->lpEnv))->DeleteLocalRef(lpJava->lpEnv, obj)

#define JNICALL_0(fName)  \
        ((*(lpJava->lpEnv))->##fName(lpJava->lpEnv))

#define JNICALL_1(fName, a1)  \
        ((*(lpJava->lpEnv))->##fName(lpJava->lpEnv, (a1)))

#define JNICALL_2(fName, a1, a2)  \
        ((*(lpJava->lpEnv))->##fName(lpJava->lpEnv, (a1), (a2)))

#define JNICALL_3(fName, a1, a2, a3)  \
        ((*(lpJava->lpEnv))->##fName(lpJava->lpEnv, (a1), (a2), (a3)))

#define JNICALL_4(fName, a1, a2, a3, a4)  \
        ((*(lpJava->lpEnv))->##fName(lpJava->lpEnv, (a1), (a2), (a3), (a4)))

typedef struct APXJAVASTDCLAZZ {
    jclass      jClazz;
    jmethodID   jMethod;
    jobject     jObject;
    jarray      jArgs;
} APXJAVASTDCLAZZ, *LPAPXJAVASTDCLAZZ;

typedef struct APXJAVAVM {
    DWORD           dwOptions;
    APXJAVASTDCLAZZ clString;
    APXJAVASTDCLAZZ clWorker;
    jint            iVersion;
    jsize           iVmCount;
    JNIEnv          *lpEnv;
    JavaVM          *lpJvm;
    /* JVM worker thread info */
    HANDLE          hWorkerThread;
    DWORD           iWorkerThread;
    DWORD           dwWorkerStatus;

} APXJAVAVM, *LPAPXJAVAVM;

#define JAVA_CLASSPATH      "-Djava.class.path="
#define JAVA_CLASSSTRING    "java/lang/String"

static __inline BOOL __apxJvmAttach(LPAPXJAVAVM lpJava)
{
    jint _iStatus = (*(lpJava->lpJvm))->GetEnv(lpJava->lpJvm,
                                          (void **)&(lpJava->lpEnv),
                                          lpJava->iVersion);
    if (_iStatus != JNI_OK) {
        if (_iStatus == JNI_EDETACHED)
            _iStatus = (*(lpJava->lpJvm))->AttachCurrentThread(lpJava->lpJvm,
                                                (void **)&(lpJava->lpEnv), NULL);
    }
    if (_iStatus != JNI_OK) {
        lpJava->lpEnv = NULL;
        return FALSE;
    }
    else
        return TRUE;
}

static __inline BOOL __apxJvmDetach(LPAPXJAVAVM lpJava)
{
    jint _iStatus = (*(lpJava->lpJvm))->DetachCurrentThread(lpJava->lpJvm);
    if (_iStatus != JNI_OK) {
        lpJava->lpEnv = NULL;
        return FALSE;
    }
    else
        return TRUE;
}

static BOOL __apxLoadJvmDll(LPCWSTR szJvmDllPath)
{
    UINT errMode;
    LPWSTR dllJvmPath = (LPWSTR)szJvmDllPath;
    DYNLOAD_FPTR_DECLARE(SetDllDirectoryW);

    if (!IS_INVALID_HANDLE(_st_sys_jvmDllHandle))
        return TRUE;    /* jvm.dll is already loaded */

    if (!dllJvmPath || *dllJvmPath == L'\0')
        dllJvmPath = apxGetJavaSoftRuntimeLib(NULL);
    if (!dllJvmPath)
        return FALSE;
    /* Suppress the not found system popup message */
    errMode = SetErrorMode(SEM_FAILCRITICALERRORS);

    _st_sys_jvmDllHandle = LoadLibraryExW(dllJvmPath, NULL, 0);
    /* This shuldn't happen, but try to search in %PATH% */
    if (IS_INVALID_HANDLE(_st_sys_jvmDllHandle))
        _st_sys_jvmDllHandle = LoadLibraryExW(dllJvmPath, NULL,
                                              LOAD_WITH_ALTERED_SEARCH_PATH);

    if (IS_INVALID_HANDLE(_st_sys_jvmDllHandle)) {
        WCHAR  jreBinPath[1024];
        DWORD  i, l = 0;

        lstrcpynW(jreBinPath, dllJvmPath, 1023);
        DYNLOAD_FPTR_ADDRESS(SetDllDirectoryW, KERNEL32);
        for (i = lstrlenW(jreBinPath); i > 0, l < 2; i--) {
            if (jreBinPath[i] == L'\\' || jreBinPath[i] == L'/') {
                jreBinPath[i] = L'\0';
                DYNLOAD_CALL(SetDllDirectoryW)(jreBinPath);
                l++;
            }
        }
        _st_sys_jvmDllHandle = LoadLibraryExW(dllJvmPath, NULL, 0);
        if (IS_INVALID_HANDLE(_st_sys_jvmDllHandle))
            _st_sys_jvmDllHandle = LoadLibraryExW(dllJvmPath, NULL,
                                                  LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    /* Restore the error mode signalization */
    SetErrorMode(errMode);
    if (IS_INVALID_HANDLE(_st_sys_jvmDllHandle)) {
        apxLogWrite(APXLOG_MARK_SYSERR);
        return FALSE;
    }
    DYNLOAD_FPTR_LOAD(JNI_GetDefaultJavaVMInitArgs, _st_sys_jvmDllHandle);
    DYNLOAD_FPTR_LOAD(JNI_CreateJavaVM,             _st_sys_jvmDllHandle);
    DYNLOAD_FPTR_LOAD(JNI_GetCreatedJavaVMs,        _st_sys_jvmDllHandle);

    if (!DYNLOAD_FPTR(JNI_GetDefaultJavaVMInitArgs) ||
        !DYNLOAD_FPTR(JNI_CreateJavaVM) ||
        !DYNLOAD_FPTR(JNI_GetCreatedJavaVMs)) {
        apxLogWrite(APXLOG_MARK_SYSERR);
        FreeLibrary(_st_sys_jvmDllHandle);
        _st_sys_jvmDllHandle = NULL;
        return FALSE;
    }

    /* Real voodo ... */
    return TRUE;
}

static BOOL __apxJavaJniCallback(APXHANDLE hObject, UINT uMsg,
                                 WPARAM wParam, LPARAM lParam)
{
    LPAPXJAVAVM lpJava;
    DWORD       dwJvmRet = 0;

    lpJava = APXHANDLE_DATA(hObject);
    switch (uMsg) {
        case WM_CLOSE:
            if (lpJava->lpJvm) {
                if (!IS_INVALID_HANDLE(lpJava->hWorkerThread)) {
                    if (GetExitCodeThread(lpJava->hWorkerThread, &dwJvmRet) &&
                        dwJvmRet == STILL_ACTIVE) {
                        TerminateThread(lpJava->hWorkerThread, 5);
                    }
                }
                SAFE_CLOSE_HANDLE(lpJava->hWorkerThread);
                __apxJvmAttach(lpJava);
                JVM_DELETE_CLAZZ(lpJava, clWorker);
                JVM_DELETE_CLAZZ(lpJava, clString);
                __apxJvmDetach(lpJava);
                /* Check if this is the jvm loader */
                if (!lpJava->iVmCount && _st_sys_jvmDllHandle) {
#if 0
                    /* Do not destroy if we terminated the worker thread */
                    if (dwJvmRet != STILL_ACTIVE)
                        (*(lpJava->lpJvm))->DestroyJavaVM(lpJava->lpJvm);
#endif
                    /* Unload JVM dll */
                    FreeLibrary(_st_sys_jvmDllHandle);
                    _st_sys_jvmDllHandle = NULL;
                }
                lpJava->lpJvm = NULL;
            }
        break;
        default:
        break;
    }
    return TRUE;
}

APXHANDLE
apxCreateJava(APXHANDLE hPool, LPCWSTR szJvmDllPath)
{

    APXHANDLE    hJava;
    LPAPXJAVAVM  lpJava;
    jsize        iVmCount;
    JavaVM       *lpJvm = NULL;

    if (!__apxLoadJvmDll(szJvmDllPath))
        return NULL;
    /*
     */
    if (DYNLOAD_FPTR(JNI_GetCreatedJavaVMs)(&lpJvm, 1, &iVmCount) != JNI_OK) {
        return NULL;
    }
    if (iVmCount && !lpJvm)
        return NULL;

    hJava = apxHandleCreate(hPool, 0,
                            NULL, sizeof(APXJAVAVM),
                            __apxJavaJniCallback);
    if (IS_INVALID_HANDLE(hJava))
        return NULL;
    hJava->dwType = APXHANDLE_TYPE_JVM;
    lpJava = APXHANDLE_DATA(hJava);
    lpJava->lpJvm = lpJvm;
    lpJava->iVmCount = iVmCount;
    return hJava;
}

static DWORD __apxMultiSzToJvmOptions(APXHANDLE hPool,
                                      LPCSTR lpString,
                                      JavaVMOption **lppArray,
                                      DWORD  nExtra)
{
    DWORD i, n = 0, l = 0;
    char *buff;
    LPSTR p;

    if (lpString) {
        l = __apxGetMultiSzLengthA(lpString, &n);
    }
    n += nExtra;
    if (IS_INVALID_HANDLE(hPool))
        buff = apxPoolAlloc(hPool, (n + 1) * sizeof(JavaVMOption) + (l + 1));
    else
        buff = apxAlloc((n + 1) * sizeof(JavaVMOption) + (l + 1));

    *lppArray = (JavaVMOption *)buff;
    p = (LPSTR)(buff + (n + 1) * sizeof(JavaVMOption));
    if (lpString)
        AplCopyMemory(p, lpString, l + 1);
    for (i = 0; i < (n - nExtra); i++) {
        DWORD qr = apxStrUnQuoteInplaceA(p);
        (*lppArray)[i].optionString = p;
        while (*p)
            p++;
        p++;
        p += qr;
    }

    return n;
}

/* a hook for a function that redirects all VM messages. */
static jint JNICALL __apxJniVfprintf(FILE *fp, const char *format, va_list args)
{
    jint rv;
    CHAR sBuf[1024+16];
    rv = wvsprintfA(sBuf, format, args);
    if (apxLogWrite(APXLOG_MARK_INFO "%s", sBuf) == 0)
        fputs(sBuf, stdout);
    return rv;
}


/* ANSI version only */
BOOL
apxJavaInitialize(APXHANDLE hJava, LPCSTR szClassPath,
                  LPCVOID lpOptions, DWORD dwMs, DWORD dwMx,
                  DWORD dwSs)
{
    LPAPXJAVAVM     lpJava;
    JavaVMInitArgs  vmArgs;
    JavaVMOption    *lpJvmOptions;
    DWORD           i, nOptions, sOptions = 2;
    BOOL            rv = FALSE;
    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return FALSE;

    lpJava = APXHANDLE_DATA(hJava);

    if (lpJava->iVmCount) {
        if (!lpJava->lpEnv && !__apxJvmAttach(lpJava)) {
            if (lpJava->iVersion == JNI_VERSION_1_2) {
                apxLogWrite(APXLOG_MARK_ERROR "Unable To Attach the JVM");
                return FALSE;
            }
            else
                lpJava->iVersion = JNI_VERSION_1_2;
            if (!__apxJvmAttach(lpJava)) {
                apxLogWrite(APXLOG_MARK_ERROR "Unable To Attach the JVM");
                return FALSE;
            }
        }
        lpJava->iVersion = JNICALL_0(GetVersion);
        if (lpJava->iVersion < JNI_VERSION_1_2) {
            apxLogWrite(APXLOG_MARK_ERROR "Unsupported JNI version %#08x", lpJava->iVersion);
            return FALSE;
        }
        rv = TRUE;
    }
    else {
        CHAR  iB[3][64];
        LPSTR szCp;
        lpJava->iVersion = JNI_VERSION_DEFAULT;
        if (dwMs)
            ++sOptions;
        if (dwMx)
            ++sOptions;
        if (dwSs)
            ++sOptions;
        nOptions = __apxMultiSzToJvmOptions(hJava->hPool, lpOptions,
                                            &lpJvmOptions, sOptions);
        szCp = apxPoolAlloc(hJava->hPool, sizeof(JAVA_CLASSPATH) + lstrlenA(szClassPath));
        lstrcpyA(szCp, JAVA_CLASSPATH);
        lstrcatA(szCp, szClassPath);
        lpJvmOptions[nOptions - sOptions].optionString = szCp;
        --sOptions;
        /* default JNI error printer */
        lpJvmOptions[nOptions - sOptions].optionString = "vfprintf";
        lpJvmOptions[nOptions - sOptions].extraInfo    = __apxJniVfprintf;
        --sOptions;
        if (dwMs) {
            wsprintfA(iB[0], "-Xms%dm", dwMs);
            lpJvmOptions[nOptions - sOptions].optionString = iB[0];
            --sOptions;
        }
        if (dwMx) {
            wsprintfA(iB[1], "-Xmx%dm", dwMx);
            lpJvmOptions[nOptions - sOptions].optionString = iB[1];
            --sOptions;
        }
        if (dwSs) {
            wsprintfA(iB[2], "-Xss%dk", dwSs);
            lpJvmOptions[nOptions - sOptions].optionString = iB[2];
            --sOptions;
        }
        for (i = 0; i < nOptions; i++) {
            apxLogWrite(APXLOG_MARK_DEBUG "Jvm Option[%d] %s", i,
                        lpJvmOptions[i].optionString);
        }
        vmArgs.options  = lpJvmOptions;
        vmArgs.nOptions = nOptions;
        vmArgs.version  = lpJava->iVersion;
        vmArgs.ignoreUnrecognized = JNI_FALSE;
        if (DYNLOAD_FPTR(JNI_CreateJavaVM)(&(lpJava->lpJvm),
                                           (void **)&(lpJava->lpEnv),
                                           &vmArgs) != JNI_OK) {
            apxLogWrite(APXLOG_MARK_ERROR "CreateJavaVM Failed");
            rv = FALSE;
        }
        else
            rv = TRUE;
        apxFree(szCp);
        apxFree(lpJvmOptions);
    }
    /* Load standard classes */
    if (rv) {
        jclass jClazz = JNICALL_1(FindClass, JAVA_CLASSSTRING);
        if (!jClazz) {
            apxLogWrite(APXLOG_MARK_ERROR "FindClass "  JAVA_CLASSSTRING " failed");
            goto cleanup;
        }
        lpJava->clString.jClazz = JNICALL_1(NewGlobalRef, jClazz);
        JNI_LOCAL_UNREF(jClazz);

        return TRUE;
    }
    else
        return FALSE;

cleanup:
    JVM_EXCEPTION_CLEAR(lpJava);
    return FALSE;
}

BOOL
apxJavaLoadMainClass(APXHANDLE hJava, LPCSTR szClassName,
                     LPCSTR szMethodName,
                     LPCVOID lpArguments)
{
    LPWSTR      *lpArgs = NULL;
    DWORD       nArgs;
    LPAPXJAVAVM lpJava;
    jclass      jClazz;

    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return FALSE;
    lpJava = APXHANDLE_DATA(hJava);
    if (!__apxJvmAttach(lpJava))
        return FALSE;

    /* Find the class */
    jClazz  = JNICALL_1(FindClass, szClassName);
    if (!jClazz) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "FindClass %s failed", szClassName);
        return FALSE;
    }
    /* Make the class global so that worker thread can attach */
    lpJava->clWorker.jClazz  = JNICALL_1(NewGlobalRef, jClazz);
    JNI_LOCAL_UNREF(jClazz);

    if (szMethodName)
        lpJava->clWorker.jMethod = JNICALL_3(GetStaticMethodID,
                                             lpJava->clWorker.jClazz,
                                             szMethodName, "([Ljava/lang/String;)V");
    else
        lpJava->clWorker.jMethod = JNICALL_3(GetStaticMethodID,
                                             lpJava->clWorker.jClazz,
                                             "main", "([Ljava/lang/String;)V");
    if (!lpJava->clWorker.jMethod) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Static method 'void main(String[])' in Class %s not found", szClassName);
        return FALSE;
    }
    nArgs = apxMultiSzToArrayW(hJava->hPool, lpArguments, &lpArgs);
    if (nArgs) {
        DWORD i;
        lpJava->clWorker.jArgs = JNICALL_3(NewObjectArray, nArgs,
                                           lpJava->clString.jClazz, NULL);
        for (i = 0; i < nArgs; i++) {
            jstring arg = JNICALL_2(NewString, lpArgs[i], lstrlenW(lpArgs[i]));
            JNICALL_3(SetObjectArrayElement, lpJava->clWorker.jArgs, i, arg);
            apxLogWrite(APXLOG_MARK_DEBUG "argv[%d] = %S", i, lpArgs[i]);
        }
    }
    apxFree(lpArgs);
    return TRUE;
}


/* Main java application worker thread
 * It will launch Java main and wait until
 * it finishes.
 */
static DWORD WINAPI __apxJavaWorkerThread(LPVOID lpParameter)
{
#define WORKER_EXIT(x)  { rv = x; goto finished; }
    DWORD rv = 0;
    LPAPXJAVAVM lpJava;
    APXHANDLE   hJava = (APXHANDLE)lpParameter;
    /* This shouldn't happen */
    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        WORKER_EXIT(0);
    lpJava = APXHANDLE_DATA(hJava);
    /* Check if we have a class and a method */
    if (!lpJava->clWorker.jClazz || !lpJava->clWorker.jMethod)
        WORKER_EXIT(2);
    if (!__apxJvmAttach(lpJava))
        WORKER_EXIT(3);
    lpJava->dwWorkerStatus = 1;
    JNICALL_3(CallStaticVoidMethod,
              lpJava->clWorker.jClazz,
              lpJava->clWorker.jMethod,
              lpJava->clWorker.jArgs);

    JVM_EXCEPTION_CLEAR(lpJava);
    __apxJvmDetach(lpJava);
finished:
    lpJava->dwWorkerStatus = 0;
    apxLogWrite(APXLOG_MARK_DEBUG "Java Worker thread finished");
    ExitThread(rv);
    /* never gets here but keep the compiler happy */
    return 0;
}


BOOL
apxJavaStart(APXHANDLE hJava)
{

    LPAPXJAVAVM lpJava;

    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return FALSE;
    lpJava = APXHANDLE_DATA(hJava);

    lpJava->hWorkerThread = CreateThread(NULL, 0, __apxJavaWorkerThread,
                                         hJava, CREATE_SUSPENDED,
                                         &lpJava->iWorkerThread);
    if (IS_INVALID_HANDLE(lpJava->hWorkerThread)) {
        apxLogWrite(APXLOG_MARK_SYSERR);
        return FALSE;
    }
    ResumeThread(lpJava->hWorkerThread);
    /* Give some time to initialize the thread */
    Sleep(1000);
    return TRUE;
}

DWORD
apxJavaWait(APXHANDLE hJava, DWORD dwMilliseconds, BOOL bKill)
{
    DWORD rv;
    LPAPXJAVAVM lpJava;

    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return FALSE;
    lpJava = APXHANDLE_DATA(hJava);

    if (!lpJava->dwWorkerStatus && lpJava->hWorkerThread)
        return WAIT_OBJECT_0;
    rv = WaitForSingleObject(lpJava->hWorkerThread, dwMilliseconds);
    if (rv == WAIT_TIMEOUT && bKill) {
        __apxJavaJniCallback(hJava, WM_CLOSE, 0, 0);
    }

    return rv;
}

LPVOID
apxJavaCreateClassV(APXHANDLE hJava, LPCSTR szClassName,
                    LPCSTR szSignature, va_list lpArgs)
{
    LPAPXJAVAVM     lpJava;
    jclass          clazz;
    jmethodID       ccont;
    jobject         cinst;

    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return NULL;
    lpJava = APXHANDLE_DATA(hJava);
    if (!__apxJvmAttach(lpJava))
        return NULL;

    clazz = JNICALL_1(FindClass, szClassName);
    if (clazz == NULL || (JVM_EXCEPTION_CHECK(lpJava))) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Could not FindClass %s", szClassName);
        return NULL;
    }

    ccont = JNICALL_3(GetMethodID, clazz, "<init>", szSignature);
    if (ccont == NULL || (JVM_EXCEPTION_CHECK(lpJava))) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Could not find Constructor %s for %s",
                    szSignature, szClassName);
        return NULL;
    }

    cinst = JNICALL_3(NewObjectV, clazz, ccont, lpArgs);
    if (cinst == NULL || (JVM_EXCEPTION_CHECK(lpJava))) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Could not create instance of %s",
                    szClassName);
        return NULL;
    }

    return cinst;
}

LPVOID
apxJavaCreateClass(APXHANDLE hJava, LPCSTR szClassName,
                   LPCSTR szSignature, ...)
{
    LPVOID rv;
    va_list args;

    va_start(args, szSignature);
    rv = apxJavaCreateClassV(hJava, szClassName, szSignature, args);
    va_end(args);

    return rv;
}

LPVOID
apxJavaCreateStringA(APXHANDLE hJava, LPCSTR szString)
{
    LPAPXJAVAVM     lpJava;
    jstring str;

    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return NULL;
    lpJava = APXHANDLE_DATA(hJava);

    str = JNICALL_1(NewStringUTF, szString);
    if (str == NULL || (JVM_EXCEPTION_CHECK(lpJava))) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Could not create string for %s",
                    szString);
        return NULL;
    }

    return str;
}

LPVOID
apxJavaCreateStringW(APXHANDLE hJava, LPCWSTR szString)
{
    LPAPXJAVAVM     lpJava;
    jstring str;

    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return NULL;
    lpJava = APXHANDLE_DATA(hJava);

    str = JNICALL_2(NewString, szString, lstrlenW(szString));
    if (str == NULL || (JVM_EXCEPTION_CHECK(lpJava))) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Could not create string for %S",
                    szString);
        return NULL;
    }

    return str;
}

jvalue
apxJavaCallStaticMethodV(APXHANDLE hJava, jclass lpClass, LPCSTR szMethodName,
                         LPCSTR szSignature, va_list lpArgs)
{
    LPAPXJAVAVM     lpJava;
    jmethodID       method;
    jvalue          rv;
    LPCSTR          s = szSignature;
    rv.l = 0;
    if (hJava->dwType != APXHANDLE_TYPE_JVM)
        return rv;
    lpJava = APXHANDLE_DATA(hJava);

    while (*s && *s != ')')
        ++s;
    if (*s != ')') {
        return rv;
    }
    else
        ++s;
    method = JNICALL_3(GetStaticMethodID, lpClass, szMethodName, szSignature);
    if (method == NULL || (JVM_EXCEPTION_CHECK(lpJava))) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Could not find method %s with signature %s",
                    szMethodName, szSignature);
        return rv;
    }
    switch (*s) {
        case 'V':
            JNICALL_3(CallStaticVoidMethodV, lpClass, method, lpArgs);
        break;
        case 'L':
        case '[':
            rv.l = JNICALL_3(CallStaticObjectMethodV, lpClass, method, lpArgs);
        break;
        case 'Z':
            rv.z = JNICALL_3(CallStaticBooleanMethodV, lpClass, method, lpArgs);
        break;
        case 'B':
            rv.b = JNICALL_3(CallStaticByteMethodV, lpClass, method, lpArgs);
        break;
        case 'C':
            rv.c = JNICALL_3(CallStaticCharMethodV, lpClass, method, lpArgs);
        break;
        case 'S':
            rv.i = JNICALL_3(CallStaticShortMethodV, lpClass, method, lpArgs);
        break;
        case 'I':
            rv.i = JNICALL_3(CallStaticIntMethodV, lpClass, method, lpArgs);
        break;
        case 'J':
            rv.j = JNICALL_3(CallStaticLongMethodV, lpClass, method, lpArgs);
        break;
        case 'F':
            rv.f = JNICALL_3(CallStaticFloatMethodV, lpClass, method, lpArgs);
        break;
        case 'D':
            rv.d = JNICALL_3(CallStaticDoubleMethodV, lpClass, method, lpArgs);
        break;
        default:
            apxLogWrite(APXLOG_MARK_ERROR "Invalid signature %s for method %s",
                        szSignature, szMethodName);
            return rv;
        break;
    }

    return rv;
}

jvalue
apxJavaCallStaticMethod(APXHANDLE hJava, jclass lpClass, LPCSTR szMethodName,
                        LPCSTR szSignature, ...)
{
    jvalue rv;
    va_list args;

    va_start(args, szSignature);
    rv = apxJavaCallStaticMethodV(hJava, lpClass, szMethodName, szSignature, args);
    va_end(args);

    return rv;
}

/* Call the Java:
 * System.setOut(new PrintStream(new FileOutputStream(filename)));
 */
BOOL
apxJavaSetOut(APXHANDLE hJava, BOOL setErrorOrOut, LPCWSTR szFilename)
{
    LPAPXJAVAVM lpJava;
    jobject     fs;
    jobject     ps;
    jstring     fn;
    jclass      sys;

    if (hJava->dwType != APXHANDLE_TYPE_JVM || !szFilename)
        return FALSE;
    lpJava = APXHANDLE_DATA(hJava);
    if (!__apxJvmAttach(lpJava))
        return FALSE;

    if ((fn = apxJavaCreateStringW(hJava, szFilename)) == NULL)
        return FALSE;
    if ((fs = apxJavaCreateClass(hJava, "java/io/FileOutputStream",
                                 "(Ljava/lang/String;Z)V", fn, JNI_TRUE)) == NULL)
        return FALSE;
    if ((ps = apxJavaCreateClass(hJava, "java/io/PrintStream",
                                 "(Ljava/io/OutputStream;)V", fs)) == NULL)
        return FALSE;
    sys = JNICALL_1(FindClass, "java/lang/System");
    if (sys == NULL || (JVM_EXCEPTION_CHECK(lpJava))) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Could not FindClass java/lang/System");
        return FALSE;
    }

    if (setErrorOrOut)
        apxJavaCallStaticMethod(hJava, sys, "setErr", "(Ljava/io/PrintStream;)V", ps);
    else
        apxJavaCallStaticMethod(hJava, sys, "setOut", "(Ljava/io/PrintStream;)V", ps);

    if (JVM_EXCEPTION_CHECK(lpJava)) {
        JVM_EXCEPTION_CLEAR(lpJava);
        apxLogWrite(APXLOG_MARK_ERROR "Error calling set method for java/lang/System");
        return FALSE;
    }
    else
        return TRUE;

}
