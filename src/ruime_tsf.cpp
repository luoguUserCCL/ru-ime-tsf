// ============================================================================
//  RuIME-TSF 1.0 — 俄语(ЙЦУКЕН)输入法 for Windows · TSF 文本服务版
// ============================================================================
//  这是一个真正的 TSF (Text Services Framework) 输入法：COM 进程内 DLL，
//  经 regsvr32 注册后会出现在 Windows 输入法列表（俄语 → RuIME JCUKEN），
//  可用 Win+Space / Ctrl+Shift 像系统输入法一样切换。
//
//  架构（最小可用 TSF 键盘 TIP）：
//    DllGetClassObject / DllCanUnloadNow   标准 COM 入口
//    DllRegisterServer / DllUnregisterServer
//        ├─ 注册 COM 类 (HKCR\CLSID\{...}\InprocServer32)
//        ├─ ITfInputProcessorProfiles::AddLanguageProfile  (语言 0x0419 俄语)
//        └─ ITfCategoryMgr::RegisterCategory(GUID_TFCAT_TIP_KEYBOARD)
//    CRuIME        实现 ITfTextInputProcessor + ITfKeyEventSink
//                  OnTestKeyDown/OnKeyDown 查 ЙЦУКЕН 键位表吃键，
//                  经 ITfContext::RequestEditSession 编辑会话插入西里尔字母
//    CEditSession 实现 ITfEditSession：替换当前选区文本
//
//  特点：
//    * 文本走 TSF 管线 —— 管理员窗口、UWP/Windows Terminal 等 TSF 宿主可用
//      （无需像钩子版那样提权；DLL 由应用进程自己加载）
//    * Scroll Lock 保留键（PreservedKey）切换 俄语/英文，状态通过命名共享
//      内存跨进程同步（每个应用进程各有一份 TIP 实例）
//    * Ctrl/Alt/Win 组合键一律放行，不影响任何快捷键
//
//  编译与注册方法见 README.md。
// ============================================================================

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601        // Windows 7 及以上
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <msctf.h>
#include <new>
#include <string>

// ============================================================================
//  本输入法的身份 GUID（自己生成，全局唯一，注册后不可再改动，
//  否则系统里会出现两个不同身份的输入法）
// ============================================================================

// {9A7B3C5D-1E4F-4A68-B2C9-7D3E8F1A6B42} —— TIP 组件 CLSID
static const CLSID CLSID_RuIME =
    {0x9a7b3c5d, 0x1e4f, 0x4a68, {0xb2, 0xc9, 0x7d, 0x3e, 0x8f, 0x1a, 0x6b, 0x42}};

// {C4E2A8F6-3B7D-4C19-9E5A-8F2B6D4C7A31} —— 语言配置文件 GUID
static const GUID GUID_RuIME_Profile =
    {0xc4e2a8f6, 0x3b7d, 0x4c19, {0x9e, 0x5a, 0x8f, 0x2b, 0x6d, 0x4c, 0x7a, 0x31}};

// {D8B4C2E9-5F31-4A76-8C2D-9E6F3B1A5C74} —— Scroll Lock 保留键 GUID
static const GUID GUID_RuIME_PreservedKey =
    {0xd8b4c2e9, 0x5f31, 0x4a76, {0x8c, 0x2d, 0x9e, 0x6f, 0x3b, 0x1a, 0x5c, 0x74}};

static const LANGID RULANG         = 0x0419;   // ru-RU 俄语
static const wchar_t RUIME_DESC[]  = L"RuIME JCUKEN (\u0419\u0426\u0423\u041A\u0415\u041D)";
static const wchar_t TOGGLE_DESC[] = L"\u5207\u6362 \u4FC4\u8BED/\u82F1\u6587 (Scroll Lock)";

// ============================================================================
//  可自定义选项
// ============================================================================

// 选中本输入法时是否默认处于俄语模式（true=俄语）
static const bool DEFAULT_ENABLED = true;

// ============================================================================
//  ЙЦУКЕН 键位映射表（\uXXXX 转义保证编码鲁棒性；注释标注实际字母）
// ============================================================================

struct MapEntry {
    WORD    vk;         // 虚拟键码
    wchar_t lo;         // 无 Shift 时的字符
    wchar_t hi;         // 有 Shift 时的字符
    bool    loDiffers;  // 无 Shift 输出与美式键盘不同才拦截
    bool    letter;     // 是否字母（受 CapsLock 影响）
};

static const MapEntry g_map[] = {
    // ---- 数字行：无 Shift 原样放行，Shift 输出俄语符号 ----
    {'1', L'1', L'!',      false, false},
    {'2', L'2', L'"',      false, false},
    {'3', L'3', L'\u2116', false, false},   // №
    {'4', L'4', L';',      false, false},
    {'5', L'5', L'%',      false, false},
    {'6', L'6', L':',      false, false},
    {'7', L'7', L'?',      false, false},
    {'8', L'8', L'*',      false, false},
    {'9', L'9', L'(',      false, false},
    {'0', L'0', L')',      false, false},
    {VK_OEM_MINUS, L'-', L'_', false, false},
    {VK_OEM_PLUS,  L'=', L'+', false, false},

    // ---- 上排字母（QWERTY 行）----
    {'Q', L'\u0439', L'\u0419', true, true},  // й Й
    {'W', L'\u0446', L'\u0426', true, true},  // ц Ц
    {'E', L'\u0443', L'\u0423', true, true},  // у У
    {'R', L'\u043A', L'\u041A', true, true},  // к К
    {'T', L'\u0435', L'\u0415', true, true},  // е Е
    {'Y', L'\u043D', L'\u041D', true, true},  // н Н
    {'U', L'\u0433', L'\u0413', true, true},  // г Г
    {'I', L'\u0448', L'\u0428', true, true},  // ш Ш
    {'O', L'\u0449', L'\u0429', true, true},  // щ Щ
    {'P', L'\u0437', L'\u0417', true, true},  // з З
    {VK_OEM_4, L'\u0445', L'\u0425', true, true},  // [ → х Х
    {VK_OEM_6, L'\u044A', L'\u042A', true, true},  // ] → ъ Ъ

    // ---- 中排字母（ASDF 行）----
    {'A', L'\u0444', L'\u0424', true, true},  // ф Ф
    {'S', L'\u044B', L'\u042B', true, true},  // ы Ы
    {'D', L'\u0432', L'\u0412', true, true},  // в В
    {'F', L'\u0430', L'\u0410', true, true},  // а А
    {'G', L'\u043F', L'\u041F', true, true},  // п П
    {'H', L'\u0440', L'\u0420', true, true},  // р Р
    {'J', L'\u043E', L'\u041E', true, true},  // о О
    {'K', L'\u043B', L'\u041B', true, true},  // л Л
    {'L', L'\u0434', L'\u0414', true, true},  // д Д
    {VK_OEM_1, L'\u0436', L'\u0416', true, true},  // ; → ж Ж
    {VK_OEM_7, L'\u044D', L'\u042D', true, true},  // ' → э Э

    // ---- 下排字母（ZXCV 行）----
    {'Z', L'\u044F', L'\u042F', true, true},  // я Я
    {'X', L'\u0447', L'\u0427', true, true},  // ч Ч
    {'C', L'\u0441', L'\u0421', true, true},  // с С
    {'V', L'\u043C', L'\u041C', true, true},  // м М
    {'B', L'\u0438', L'\u0418', true, true},  // и И
    {'N', L'\u0442', L'\u0422', true, true},  // т Т
    {'M', L'\u044C', L'\u042C', true, true},  // ь Ь
    {VK_OEM_COMMA,  L'\u0431', L'\u0411', true, true},  // , → б Б
    {VK_OEM_PERIOD, L'\u044E', L'\u042E', true, true},  // . → ю Ю
    {VK_OEM_2, L'.', L',', true, false},                // / → . 和 ,

    // ---- 其它 ----
    {VK_OEM_3, L'\u0451', L'\u0401', true, true},  // ` → ё Ё
    {VK_OEM_5, L'\\', L'/', false, false},         // \ → \ 和 /
};

static const MapEntry* Lookup(UINT vk)
{
    for (const MapEntry& e : g_map)
        if (e.vk == vk) return &e;
    return nullptr;
}

static wchar_t MapChar(const MapEntry& e, bool shift, bool caps)
{
    if (e.letter)
        return (shift != caps) ? e.hi : e.lo;   // Caps 打开时大小写互换
    return shift ? e.hi : e.lo;
}

static bool ShouldTranslate(const MapEntry& e, bool shift)
{
    return shift ? true : e.loDiffers;
}

// ============================================================================
//  俄语/英文状态 —— 命名共享内存，跨进程同步所有 TIP 实例
// ============================================================================

struct RuSharedState {
    LONG ruEnabled;
};

static RuSharedState* AcquireSharedState()
{
    static RuSharedState* s_pShared = nullptr;   // 每进程仅映射一次
    if (!s_pShared) {
        HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                         PAGE_READWRITE, 0, sizeof(RuSharedState),
                                         L"Local\\RuIME_TSF_SharedState");
        if (hMap) {
            const bool firstCreator = (GetLastError() != ERROR_ALREADY_EXISTS);
            void* pView = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RuSharedState));
            if (pView) {
                if (firstCreator)
                    ((RuSharedState*)pView)->ruEnabled = DEFAULT_ENABLED ? 1 : 0;
                s_pShared = (RuSharedState*)pView;
            }
            // hMap 有意不 CloseHandle：映射视图的存续依赖它，随进程退出回收
        }
    }
    return s_pShared;
}

static bool RuEnabled()
{
    RuSharedState* p = AcquireSharedState();
    return p ? (p->ruEnabled != 0) : DEFAULT_ENABLED;
}

static void SetRuEnabled(bool on)
{
    RuSharedState* p = AcquireSharedState();
    if (p) InterlockedExchange(&p->ruEnabled, on ? 1 : 0);
}

// ============================================================================
//  模块级状态
// ============================================================================

static volatile LONG g_cObjects = 0;   // 存活 COM 对象数
static volatile LONG g_cLock    = 0;   // LockServer 引用
static HINSTANCE      g_hinst   = nullptr;

// ============================================================================
//  按键判定
// ============================================================================

static bool ModifierHeld()
{
    const SHORT kDown = (SHORT)0x8000;
    return (GetAsyncKeyState(VK_CONTROL) & kDown) != 0 ||
           (GetAsyncKeyState(VK_MENU)    & kDown) != 0 ||
           (GetAsyncKeyState(VK_LWIN)    & kDown) != 0 ||
           (GetAsyncKeyState(VK_RWIN)    & kDown) != 0;
}

// 该击键是否应被本输入法吃掉并转换（OnTestKeyDown / OnKeyDown 共用同一判定）
static bool WantEatKey(UINT vk)
{
    if (!RuEnabled() || ModifierHeld())
        return false;
    const MapEntry* e = Lookup(vk);
    if (!e)
        return false;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & (SHORT)0x8000) != 0;
    return ShouldTranslate(*e, shift);
}

static wchar_t CharForKey(UINT vk)
{
    const MapEntry* e = Lookup(vk);
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & (SHORT)0x8000) != 0;
    const bool caps  = (GetKeyState(VK_CAPITAL) & 1) != 0;
    return e ? MapChar(*e, shift, caps) : 0;
}

// ============================================================================
//  CEditSession —— 编辑会话：把字符写入当前选区
// ============================================================================

class CEditSession : public ITfEditSession
{
public:
    CEditSession(ITfContext* pic, wchar_t ch)
        : m_cRef(1), m_pic(pic), m_ch(ch)
    {
        if (m_pic) m_pic->AddRef();
        InterlockedIncrement(&g_cObjects);
    }
    virtual ~CEditSession()
    {
        if (m_pic) m_pic->Release();
        InterlockedDecrement(&g_cObjects);
    }

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_ITfEditSession)) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return InterlockedIncrement(&m_cRef); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG r = InterlockedDecrement(&m_cRef);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }

    // ---- ITfEditSession：拿到编辑锁后执行 ----
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie ec) override
    {
        if (!m_pic) return S_OK;
        // 取当前选区（含光标位置），用 SetText 写入 —— 光标处插入 / 选区被替换
        TF_SELECTION sel = {};
        ULONG fetched = 0;
        if (FAILED(m_pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched == 0)
            return S_OK;
        if (sel.range) {
            sel.range->SetText(ec, 0, &m_ch, 1);
            sel.range->Release();
        }
        return S_OK;
    }

private:
    LONG         m_cRef;
    ITfContext*  m_pic;
    wchar_t      m_ch;
};

// ============================================================================
//  CRuIME —— TIP 主体：ITfTextInputProcessor + ITfKeyEventSink
// ============================================================================

class CRuIME : public ITfTextInputProcessor, public ITfKeyEventSink
{
public:
    CRuIME()
        : m_cRef(1), m_ptim(nullptr), m_tid(0), m_dwKeySinkCookie(0), m_pKeystrokeMgr(nullptr)
    {
        InterlockedIncrement(&g_cObjects);
    }
    virtual ~CRuIME()
    {
        Deactivate();
        InterlockedDecrement(&g_cObjects);
    }

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_ITfTextInputProcessor)) {
            *ppv = static_cast<ITfTextInputProcessor*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualGUID(riid, IID_ITfKeyEventSink)) {
            *ppv = static_cast<ITfKeyEventSink*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return InterlockedIncrement(&m_cRef); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG r = InterlockedDecrement(&m_cRef);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }

    // ---- ITfTextInputProcessor：被系统选为当前输入法时调用 ----
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* ptim, TfClientId tid) override
    {
        if (!ptim) return E_INVALIDARG;
        Deactivate();                       // 防御：重复 Activate 先清理

        m_ptim = ptim;
        m_ptim->AddRef();
        m_tid = tid;

        // 1) 挂键盘事件汇：TSF 把击键先交给本输入法裁决
        ITfSource* pSrc = nullptr;
        if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfSource, (void**)&pSrc)) && pSrc) {
            pSrc->AdviseSink(IID_ITfKeyEventSink,
                             static_cast<IUnknown*>(static_cast<ITfKeyEventSink*>(this)),
                             &m_dwKeySinkCookie);
            pSrc->Release();
        }

        // 2) 注册 Scroll Lock 保留键（俄语/英文切换）
        if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfKeystrokeMgr, (void**)&m_pKeystrokeMgr)) && m_pKeystrokeMgr) {
            TF_PRESERVEDKEY pk;
            pk.uVKey      = VK_SCROLL;
            pk.uModifiers = 0;
            m_pKeystrokeMgr->PreserveKey(m_tid, GUID_RuIME_PreservedKey, &pk,
                                         TOGGLE_DESC, (ULONG)wcslen(TOGGLE_DESC));
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Deactivate() override
    {
        if (m_pKeystrokeMgr) {
            TF_PRESERVEDKEY pk;
            pk.uVKey      = VK_SCROLL;
            pk.uModifiers = 0;
            m_pKeystrokeMgr->UnpreserveKey(GUID_RuIME_PreservedKey, &pk);
            m_pKeystrokeMgr->Release();
            m_pKeystrokeMgr = nullptr;
        }
        if (m_ptim) {
            if (m_dwKeySinkCookie) {
                ITfSource* pSrc = nullptr;
                if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfSource, (void**)&pSrc)) && pSrc) {
                    pSrc->UnadviseSink(m_dwKeySinkCookie);
                    pSrc->Release();
                }
                m_dwKeySinkCookie = 0;
            }
            m_ptim->Release();
            m_ptim = nullptr;
        }
        m_tid = 0;
        return S_OK;
    }

    // ---- ITfKeyEventSink ----
    // 注意：ITfKeyEventSink::OnSetFocus 的参数是 fForeground（本线程是否到前台），
    // 与 ITfThreadMgrEventSink::OnSetFocus(两个文档管理器) 不是同一个接口
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM,
                                            BOOL* pfEaten) override
    {
        *pfEaten = (pic && WantEatKey((UINT)(wParam & 0xFF))) ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM,
                                        BOOL* pfEaten) override
    {
        *pfEaten = FALSE;
        if (!pic || !WantEatKey((UINT)(wParam & 0xFF)))
            return S_OK;

        const wchar_t ch = CharForKey((UINT)(wParam & 0xFF));
        if (ch && SUCCEEDED(InsertChar(pic, ch)))
            *pfEaten = TRUE;                // 仅在成功插入后才吃键，失败则放行原键
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) override
    {
        *pfEaten = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) override
    {
        *pfEaten = FALSE;
        return S_OK;
    }

    // Scroll Lock 保留键触发：切换 俄语/英文（跨进程共享状态）
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*, REFGUID rguid, BOOL* pfEaten) override
    {
        *pfEaten = FALSE;
        if (IsEqualGUID(rguid, GUID_RuIME_PreservedKey)) {
            SetRuEnabled(!RuEnabled());
            *pfEaten = TRUE;
        }
        return S_OK;
    }

private:
    // 经同步编辑会话插入字符；宿主拒绝同步锁时退回异步
    HRESULT InsertChar(ITfContext* pic, wchar_t ch)
    {
        CEditSession* pes = new (std::nothrow) CEditSession(pic, ch);
        if (!pes) return E_OUTOFMEMORY;

        HRESULT hrSession = E_FAIL;
        HRESULT hr = pic->RequestEditSession(m_tid, pes, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (hr != S_OK)                     // 键事件汇中通常允许同步锁；个别宿主不允许
            hr = pic->RequestEditSession(m_tid, pes, TF_ES_READWRITE, &hrSession);

        pes->Release();
        return SUCCEEDED(hr) ? hrSession : hr;
    }

    LONG             m_cRef;
    ITfThreadMgr*    m_ptim;
    TfClientId       m_tid;
    DWORD            m_dwKeySinkCookie;
    ITfKeystrokeMgr* m_pKeystrokeMgr;
};

// ============================================================================
//  CClassFactory
// ============================================================================

class CRuIMEFactory : public IClassFactory
{
public:
    CRuIMEFactory() : m_cRef(1) { InterlockedIncrement(&g_cObjects); }
    virtual ~CRuIMEFactory()    { InterlockedDecrement(&g_cObjects); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return InterlockedIncrement(&m_cRef); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG r = InterlockedDecrement(&m_cRef);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        CRuIME* p = new (std::nothrow) CRuIME();
        if (!p) return E_OUTOFMEMORY;
        const HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override
    {
        if (fLock) InterlockedIncrement(&g_cLock);
        else       InterlockedDecrement(&g_cLock);
        return S_OK;
    }

private:
    LONG m_cRef;
};

// ============================================================================
//  标准 COM DLL 导出
// ============================================================================

extern "C" HRESULT STDMETHODCALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (!IsEqualGUID(rclsid, CLSID_RuIME))
        return CLASS_E_CLASSNOTAVAILABLE;

    CRuIMEFactory* pFactory = new (std::nothrow) CRuIMEFactory();
    if (!pFactory) return E_OUTOFMEMORY;
    const HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

extern "C" HRESULT STDMETHODCALLTYPE DllCanUnloadNow()
{
    return (g_cObjects == 0 && g_cLock == 0) ? S_OK : S_FALSE;
}

// ============================================================================
//  注册 / 反注册（regsvr32 调用，需要管理员权限写 HKCR）
// ============================================================================

static bool WriteRegSz(HKEY root, const wchar_t* subkey, const wchar_t* name, const wchar_t* data)
{
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG r = RegSetValueExW(k, name, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(data),
                                  static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

extern "C" HRESULT STDMETHODCALLTYPE DllRegisterServer()
{
    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(g_hinst, dllPath, MAX_PATH);

    wchar_t clsidStr[64] = {};
    StringFromGUID2(CLSID_RuIME, clsidStr, 64);

    // ---- 1) COM 组件注册 ----
    const std::wstring clsKey = std::wstring(L"CLSID\\") + clsidStr;
    const std::wstring inproc = clsKey + L"\\InprocServer32";
    bool ok = WriteRegSz(HKEY_CLASSES_ROOT, clsKey.c_str(), nullptr, L"RuIME JCUKEN Text Service");
    ok = ok && WriteRegSz(HKEY_CLASSES_ROOT, inproc.c_str(), nullptr, dllPath);
    ok = ok && WriteRegSz(HKEY_CLASSES_ROOT, inproc.c_str(), L"ThreadingModel", L"Apartment");
    if (!ok) return E_FAIL;

    // ---- 2) 注册为 TSF 输入法（俄语 0x0419）----
    const bool comInitHere = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    HRESULT hr = E_FAIL;
    {
        ITfInputProcessorProfiles* pip = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                                       (void**)&pip)) && pip) {
            hr = pip->AddLanguageProfile(CLSID_RuIME, RULANG, GUID_RuIME_Profile,
                                         RUIME_DESC, (ULONG)wcslen(RUIME_DESC),
                                         dllPath, (ULONG)wcslen(dllPath), 0);
            pip->Release();
        }
        if (SUCCEEDED(hr)) {
            ITfCategoryMgr* pcm = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                           CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                           (void**)&pcm)) && pcm) {
                pcm->RegisterCategory(CLSID_RuIME, GUID_TFCAT_TIP_KEYBOARD, CLSID_RuIME);
                pcm->Release();
            }
        }
    }
    if (comInitHere) CoUninitialize();
    return hr;
}

extern "C" HRESULT STDMETHODCALLTYPE DllUnregisterServer()
{
    const bool comInitHere = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    {
        ITfInputProcessorProfiles* pip = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                                       (void**)&pip)) && pip) {
            pip->RemoveLanguageProfile(CLSID_RuIME, RULANG, GUID_RuIME_Profile);
            pip->Release();
        }
        ITfCategoryMgr* pcm = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                       (void**)&pcm)) && pcm) {
            pcm->UnregisterCategory(CLSID_RuIME, GUID_TFCAT_TIP_KEYBOARD, CLSID_RuIME);
            pcm->Release();
        }
    }
    if (comInitHere) CoUninitialize();

    // 删除 COM 类注册（best effort）
    wchar_t clsidStr[64] = {};
    StringFromGUID2(CLSID_RuIME, clsidStr, 64);
    const std::wstring clsKey = std::wstring(L"CLSID\\") + clsidStr;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, clsKey.c_str());
    return S_OK;
}

// ============================================================================
//  DllMain
// ============================================================================

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        g_hinst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
