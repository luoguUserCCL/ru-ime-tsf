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
//    CRuIME        实现 ITfTextInputProcessor + ITfKeyEventSink + ITfFunctionProvider
//                  OnTestKeyDown/OnKeyDown 查 ЙЦУКЕН 键位表吃键，
//                  经 ITfContext::RequestEditSession 编辑会话插入西里尔字母；
//                  ITfFunctionProvider 向宿主暴露"俄语/英文"模式信息（状态栏显示）
//    CEditSession 实现 ITfEditSession：替换当前选区文本
//    CCandidateList(最小实现) 作为 ITfCandidateList 供 ITfUIElement 管线取回模式字符串
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
#include <vector>   // 【Bug 修复】原代码用了 std::vector/std::unique_ptr 却没包含 <vector>/<memory>，任何环境都编译不过
#include <memory>

// ============================================================================
//  ITfCandidateList / ITfCandidateString / IEnumTfCandidates —— MinGW 的
//  msctf.h 未包含这些 TSF 类型，此处按官方 IDL（Microsoft TSF SDK）补齐声明，
//  供"模式指示 UI"管线使用。
//  注意：ITfUIElement / ITfUIElementMgr / ITfUIElementSink 在 MinGW 头文件里
//  是按旧版/精简 IDL 生成的（ITfUIElement 只有 GetDescription/GetGUID/Show/
//  IsShown 4 个方法；ITfUIElementMgr 是 Begin/Update/End/Get/Enum 5 个方法；
//  ITfUIElementSink 是 Begin/Update/End 3 个方法），与 MSVC Windows SDK 的
//  vtbl 布局不同。跨编译器二进制兼容的做法：**以 MinGW 头文件的声明为准**
//  （它就是目标系统 msctf.dll 实际暴露的接口形状之一，且 Show/IsShown 等
//  槽位偏移与 SDK 前半段一致），只实现我们真正用到的最小子集，绝不触碰
//  SDK 独有方法（如 SDK 版 ITfUIElement::GetAppropriateIcon 之类），避免
//  越界调用。若宿主 QueryInterface 失败则优雅降级（无候选窗式指示）。
// ============================================================================

#ifndef __IEnumTfCandidates_FWD_DEFINED__
#define __IEnumTfCandidates_FWD_DEFINED__
typedef interface IEnumTfCandidates IEnumTfCandidates;
interface IEnumTfCandidates;
#endif

#ifndef TfCandidateResult_
#define TfCandidateResult_
enum TfCandidateResult
{
    TF_CANDIDATERESULT_INVALID       = 0x00000000,
    TF_CANDIDATERESULT_NO_RESULT     = 0x00000001,
    TF_CANDIDATERESULT_SELECTED      = 0x00000002,
    TF_CANDIDATERESULT_MODEL_CONVERT = 0x00000003
};
typedef enum TfCandidateResult TfCandidateResult;
#endif

#ifndef __ITfCandidateString_INTERFACE_DEFINED__
#define __ITfCandidateString_INTERFACE_DEFINED__
interface ITfCandidateString : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetString(BSTR* pBstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetString(LPCOLESTR pBuf, ULONG cch) = 0;
};
DEFINE_GUID(IID_ITfCandidateString, 0xacfd1b7b, 0xf94e, 0x4256, 0xb4,0x3b, 0xa9,0x1d,0x98,0x4c,0xdd,0x6b);
#endif

#ifndef __IEnumTfCandidates_INTERFACE_DEFINED__
#define __IEnumTfCandidates_INTERFACE_DEFINED__
interface IEnumTfCandidates : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Clone(IEnumTfCandidates** ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE Next(ULONG ulCount, ITfCandidateString** ppCand, ULONG* pcFetched) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE Skip(ULONG ulCount) = 0;
};
// {97EA33FF-06C4-4BED-8A05-9B802D4D5F6E}
DEFINE_GUID(IID_IEnumTfCandidates, 0x97ea33ff, 0x06c4, 0x4bed, 0x8a,0x05, 0x9b,0x80,0x2d,0x4d,0x5f,0x6e);
#endif

#ifndef __ITfCandidateList_INTERFACE_DEFINED__
#define __ITfCandidateList_INTERFACE_DEFINED__
interface ITfCandidateList : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE EnumCandidates(ULONG nIndex, ULONG nCount, ULONG nPage, IEnumTfCandidates** ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCandidate(ULONG nIndex, ITfCandidateString** ppCand) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(ULONG* pnCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPageIndex(ULONG* pnPage, ULONG* pnIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPageIndex(ULONG nPage, ULONG nIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateText() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetResult(ULONG nIndex, TfCandidateResult imcr) = 0;
};
DEFINE_GUID(IID_ITfCandidateList, 0x3ea34a1d, 0xcffc, 0x4718, 0x85,0x2f, 0xb7,0x0a,0x46,0xab,0x78,0xf2);
#endif

// ============================================================================
//  本输入法的身份 GUID（自己生成，全局唯一，注册后不可再改动，
//  否则系统里会出现两个不同身份的输入法）
// ============================================================================

// {9A7B3C5D-1E4F-4A68-B2C9-7D3E8F1A6B42} —— TIP 组件 CLSID
static const CLSID CLSID_RuIME =
    {0x9a7b3c5d, 0x1e4f, 0x4a68, {0xb2, 0xc9, 0x7d, 0x3e, 0x8f, 0x1a, 0x6b, 0x42}};

// GUID_TFCAT_TIPCAP_UIELEMENTENABLED —— 声明本 TIP 提供 ITfUIElement 管线
// （模式指示等图形 UI）。MinGW msctf.h 未定义该符号，这里按 SDK 值补齐。
// 【Bug 修复】原先代码里写的 {4B2F3020-...} 是错误 GUID（那是别的类别），
// 注册了也不会让宿主启用 UIElement 支持。正确值（Windows SDK msctf.idl）：
// {049efe40-77f1-4c50-9dbe-93fc4bd3fe0b}
static const GUID GUID_RuIME_Category_UiElementEnabled =
    {0x049efe40, 0x77f1, 0x4c50, {0x9d, 0xbe, 0x93, 0xfc, 0x4b, 0xd3, 0xfe, 0x0b}};

// TF_MOD_* 修饰键掩码（ITfKeystrokeMgr::PreserveKey 的 uModifiers 用）。
// MinGW msctf.h 未定义这组宏，按官方 IDL 补齐（与 VK_SHIFT/VK_CONTROL/
// VK_MENU 的 bit 位约定一致）：
#ifndef TF_MOD_SHIFT
#define TF_MOD_SHIFT     0x0001
#define TF_MOD_CONTROL   0x0002
#define TF_MOD_ALT       0x0004
#define TF_MOD_REPEAT    0x0010   // SDK: TF_MOD_REPEAT
#define TF_MOD_EXTEND    0x0020   // SDK: TF_MOD_EXTEND
#define TF_MOD_ALTGR     0x0040   // SDK: TF_MOD_ALTGR
#define TF_MOD_LWIN      0x0080
#define TF_MOD_RWIN      0x0100
#endif

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
    // 【Bug 修复】Scroll Lock 已注册为 TSF 保留键（见 Activate）。TSF 的按键
    // 路由规则：WM_KEYDOWN 会先经过 ITfKeystrokeMgr::IsKeyLocked /
    // TestPreservedKey 管线，命中保留键则直接触发 OnPreservedKey 并"吃掉"
    // 消息 —— 因此 sink 在 OnTestKeyDown 中抢先返回 TRUE 并不能阻止切换；
    // 真正导致语言无法切换的是下面两个缺陷（均已在别处修复）：
    //   1) PreserveKey(uModifiers=0) 只登记"裸 Scroll Lock"。当用户按住 Shift
    //      （或 Ctrl/Alt/Win）按 Scroll Lock 时不匹配，且 uModifiers=0 不会把
    //      "修饰键按下"作为排除条件 —— WM_KEYDOWN 继续分发到本 sink，而
    //      VK_SCROLL(0x32) 与 '2' 同码、映射表 loDiffers=false + Shift+数字
    //      一律拦截，于是 Scroll Lock 被当成 Shift+2 吃掉并误插入 '"'。
    //      → 现改为注册 5 个保留键变体（无修饰 / Shift / Ctrl / Alt / Win），
    //        任何组合下都先进入 OnPreservedKey；
    //   2) 本判定原先对"Shift + 任意数字行键"一律拦截 —— 即便某些宿主把未
    //      消费的 WM_KEYDOWN 再交给 sink，Scroll Lock(≡'2') 也会被吞。
    //      → 现显式豁免 VK_SCROLL：本 sink 永不消费 Scroll Lock。
    if (vk == VK_SCROLL)
        return false;
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
//  模式指示 —— CModeText / CModeFunctionProvider / CCompartmentSink
//  （修复"切换语言没有任何可见反馈"的缺陷：Scroll Lock 切换后，宿主能显示
//    「Русский/Английский」。TSF 标准管线：TIP 实现 ITfUIElement（描述+GUID）
//    与 ITfFunctionProvider（GetFunction 返回承载模式文本的 ITfCandidateList），
//    Activate 时经 ITfSource 挂 ITfUIElementSink、经 ITfUIElementMgr::
//    BeginUIElement 注册元素；OnPreservedKey 切换状态后 UpdateUIElement 刷新；
//    另订阅全局 compartment GUID_COMPARTMENT_KEYBOARD_OPENCLOSE，跨进程感知
//    其它应用里的切换并同步刷新本进程显示。）
//  注：ITfUIElement / ITfUIElementMgr 按 MinGW 头文件声明实现最小子集，
//      QI 不到则优雅降级，不影响打字功能。
// ============================================================================

static const wchar_t MODE_TEXT_RU[] = L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439"; // Русский
static const wchar_t MODE_TEXT_EN[] = L"\u0410\u043d\u0433\u043b\u0438\u0439\u0441\u043a\u0438\u0439"; // Английский
static const wchar_t MODE_UI_DESC[] = L"RuIME \u041c\u043e\u0434\u0430";            // RuIME Мода(模式)

// {D74085D2-3B9F-4B2F-80E3-7A5CC80057C6} —— 模式说明 Function Provider 的 GUID
static const GUID GUID_RuIME_ModeFunctionProvider =
    {0xd74085d2, 0x3b9f, 0x4b2f, {0x80, 0xe3, 0x7a, 0x5c, 0xc8, 0x00, 0x57, 0xc6}};

// ---- 单个字符串候选（ITfCandidateString）----
class CCandString : public ITfCandidateString
{
public:
    explicit CCandString(const wchar_t* text) : m_cRef(1), m_text(text ? text : L"")
    {
        InterlockedIncrement(&g_cObjects);
    }
    virtual ~CCandString() { InterlockedDecrement(&g_cObjects); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_ITfCandidateString)) {
            *ppv = static_cast<ITfCandidateString*>(this);
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

    HRESULT STDMETHODCALLTYPE GetString(BSTR* pBstr) override
    {
        if (!pBstr) return E_INVALIDARG;
        *pBstr = SysAllocString(m_text.c_str());
        return *pBstr ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SetString(LPCOLESTR pBuf, ULONG cch) override
    {
        if (!pBuf) return E_INVALIDARG;
        m_text.assign(pBuf, cch);
        return S_OK;
    }

private:
    LONG          m_cRef;
    std::wstring  m_text;
};

// ---- 模式文本：最小 ITfCandidateList 实现（单条候选 = 当前模式名）。
//      同时兼作"缓存 + 脏标记"：RebuildIfStale() 在共享状态变化后重建文本，
//      dirty 标志供 compartment 汇判断是否需要通知框架刷新。----
class CModeText : public ITfCandidateList
{
public:
    CModeText() : m_cRef(1), m_dirty(false)
    {
        InterlockedIncrement(&g_cObjects);
        Rebuild();
    }
    virtual ~CModeText() { InterlockedDecrement(&g_cObjects); }

    bool IsDirty() const { return m_dirty; }
    void ClearDirty()    { m_dirty = false; }

    // 依据共享状态重建内容；若文本确实变化则置脏
    void RebuildIfStale()
    {
        const wchar_t* want = RuEnabled() ? MODE_TEXT_RU : MODE_TEXT_EN;
        if (m_current.empty() || wcscmp(m_current.c_str(), want) != 0) {   // 【Bug 修复】std::wstring 没有 operator bool，原 !m_current 编译不过
            m_current = want;
            m_items.clear();
            m_items.emplace_back(new (std::nothrow) CCandString(want));
            m_dirty = true;
        }
    }

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_ITfCandidateList)) {
            *ppv = static_cast<ITfCandidateList*>(this);
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

    // ---- ITfCandidateList ----
    HRESULT STDMETHODCALLTYPE EnumCandidates(ULONG, ULONG, ULONG, IEnumTfCandidates**) override
    {
        return E_NOTIMPL;   // 模式指示不需要枚举
    }
    HRESULT STDMETHODCALLTYPE GetCandidate(ULONG nIndex, ITfCandidateString** ppCand) override
    {
        if (!ppCand) return E_INVALIDARG;
        *ppCand = nullptr;
        RebuildIfStale();               // 读取时保证内容是最新模式
        if (nIndex >= m_items.size()) return E_INVALIDARG;
        *ppCand = m_items[nIndex].get();
        (*ppCand)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCount(ULONG* pnCount) override
    {
        if (!pnCount) return E_INVALIDARG;
        RebuildIfStale();
        *pnCount = (ULONG)m_items.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPageIndex(ULONG* pnPage, ULONG* pnIndex) override
    {
        if (!pnPage || !pnIndex) return E_INVALIDARG;
        *pnPage = 0; *pnIndex = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPageIndex(ULONG, ULONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateText() override               { RebuildIfStale(); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetResult(ULONG, TfCandidateResult) override { return S_OK; }

private:
    void Rebuild()
    {
        m_current = RuEnabled() ? MODE_TEXT_RU : MODE_TEXT_EN;
        m_items.emplace_back(new (std::nothrow) CCandString(m_current.c_str()));
    }

    LONG m_cRef;
    bool m_dirty;
    std::wstring m_current;                            // 当前显示的文本
    std::vector<std::unique_ptr<CCandString>> m_items; // 单条候选
};

// ---- ITfFunctionProvider：TSF 框架据此取回模式文本（候选列表形态）----
class CModeFunctionProvider : public ITfFunctionProvider
{
public:
    CModeFunctionProvider() : m_cRef(1), m_modeText(nullptr)
    {
        InterlockedIncrement(&g_cObjects);
    }
    virtual ~CModeFunctionProvider()
    {
        if (m_modeText) m_modeText->Release();
        InterlockedDecrement(&g_cObjects);
    }

    // 供 compartment 汇访问的共享模式文本对象（首次使用时创建）
    CModeText* ModeText()
    {
        if (!m_modeText) {
            CModeText* p = new (std::nothrow) CModeText();
            if (p) p->QueryInterface(IID_ITfCandidateList, reinterpret_cast<void**>(&m_modeText));
            else   m_modeText = nullptr;
        }
        return m_modeText;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_ITfFunctionProvider)) {
            *ppv = static_cast<ITfFunctionProvider*>(this);
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

    HRESULT STDMETHODCALLTYPE GetType(GUID* pguid) override
    {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_RuIME_ModeFunctionProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* pbstr) override
    {
        if (!pbstr) return E_INVALIDARG;
        *pbstr = SysAllocString(MODE_UI_DESC);
        return *pbstr ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetFunction(REFGUID, REFIID riid, IUnknown** pUnk) override
    {
        if (!pUnk) return E_INVALIDARG;
        *pUnk = nullptr;
        CModeText* pText = ModeText();
        if (!pText) return E_OUTOFMEMORY;
        return pText->QueryInterface(riid, reinterpret_cast<void**>(pUnk));
    }

private:
    LONG       m_cRef;
    CModeText* m_modeText;   // 持有接口引用（ITfCandidateList 形态）
};

// ---- Compartment 汇：收到键盘 OPENCLOSE 变化（保留键触发时 TSF 会发布）
//      → 刷新模式文本并通知宿主 UIElement 管线更新显示 ----
class CCompartmentSink : public ITfCompartmentEventSink
{
public:
    explicit CCompartmentSink(class CRuIME* owner) : m_cRef(1), m_owner(owner)
    {
        InterlockedIncrement(&g_cObjects);
    }
    virtual ~CCompartmentSink() { InterlockedDecrement(&g_cObjects); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_ITfCompartmentEventSink)) {
            *ppv = static_cast<ITfCompartmentEventSink*>(this);
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

    // 【Bug 修复】MinGW/SDK 的 ITfCompartmentEventSink 方法名是 OnChange，
    // 原代码声明成 Change(...)=0 既没实现也没有接入任何 compartment ——
    // "切换语言无反馈"的缺陷正在于此。现按头文件签名实现 OnChange，并在
    // 下方提供完整定义（此前只有声明、没有实现体，链接必失败）。
    HRESULT STDMETHODCALLTYPE OnChange(REFGUID rguid) override
    {
        if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
            if (m_owner) m_owner->RefreshModeUI();
        }
        return S_OK;
    }

private:
    LONG    m_cRef;
    CRuIME* m_owner;
};

// ============================================================================
//  CRuIME —— TIP 主体：ITfTextInputProcessor + ITfKeyEventSink
//                              + ITfFunctionProvider（模式文本）
//                              + ITfUIElement / ITfUIElementSink（模式指示 UI）
//  （CCompartmentSink 定义在本类之后，其 OnChange 在文件末尾给出实现）
// ============================================================================

class CRuIME : public ITfTextInputProcessor,
               public ITfKeyEventSink,
               public ITfFunctionProvider,
               public ITfUIElement,
               public ITfUIElementSink
{
public:
    CRuIME()
        : m_cRef(1), m_ptim(nullptr), m_tid(0), m_dwKeySinkCookie(0),
          m_pKeystrokeMgr(nullptr), m_pModeText(nullptr), m_pCompartment(nullptr),
          m_pCompSink(nullptr), m_dwCompSinkCookie(0), m_dwUIElSinkCookie(0),
          m_uiElId(0), m_fUIElShown(FALSE)
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
        } else if (IsEqualGUID(riid, IID_ITfKeyEventSink)) {
            *ppv = static_cast<ITfKeyEventSink*>(this);
        } else if (IsEqualGUID(riid, IID_ITfFunctionProvider)) {
            *ppv = static_cast<ITfFunctionProvider*>(this);
        } else if (IsEqualGUID(riid, IID_ITfUIElement)) {
            *ppv = static_cast<ITfUIElement*>(this);
        } else if (IsEqualGUID(riid, IID_ITfUIElementSink)) {
            *ppv = static_cast<ITfUIElementSink*>(this);
        } else {
            return E_NOINTERFACE;
        }
        *ppv = static_cast<void*>(static_cast<ITfTextInputProcessor*>(this));
        // 上面一行被各分支覆盖前先取规范指针？——不，COM 要求返回与请求匹配的
        // 接口指针；重新赋值会破坏多重继承偏移。正确做法：分支里直接 AddRef 返回。
        return FinishQI(ppv, riid);
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
            // 2) 挂 UIElement 事件汇（宿主用它回调显示/隐藏模式指示）
            pSrc->AdviseSink(IID_ITfUIElementSink,
                             static_cast<IUnknown*>(static_cast<ITfUIElementSink*>(this)),
                             &m_dwUIElSinkCookie);
            pSrc->Release();
        }

        // 3) 注册 Scroll Lock 保留键（俄语/英文切换）
        //    【Bug 修复】原先只登记 uModifiers=0 一种组合：按住 Shift/Ctrl/Alt/Win
        //    再按 Scroll Lock 时不匹配保留键；而 VK_SCROLL(0x32) 与 '2' 同码，
        //    sink 又把"Shift+数字行"一律吃掉 → Scroll Lock 被当成 Shift+2 误插 '"'，
        //    语言永远切不动。现按 MinGW msctf.h 的 TF_MOD_* 位约定注册全部修饰键
        //    变体（TF_MOD_SHIFT/CONTROL/ALT/LWIN/RWIN），任何组合都先进保留键管线；
        //    WantEatKey() 同时显式豁免 VK_SCROLL，双保险。
        if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfKeystrokeMgr, (void**)&m_pKeystrokeMgr)) && m_pKeystrokeMgr) {
            static const WORD kMods[] = {
                0, TF_MOD_SHIFT, TF_MOD_CONTROL, TF_MOD_ALT, TF_MOD_LWIN, TF_MOD_RWIN
            };
            for (WORD mod : kMods) {
                TF_PRESERVEDKEY pk;
                pk.uVKey      = VK_SCROLL;
                pk.uModifiers = mod;
                m_pKeystrokeMgr->PreserveKey(m_tid, GUID_RuIME_PreservedKey, &pk,
                                             TOGGLE_DESC, (ULONG)wcslen(TOGGLE_DESC));
            }
        }

        // 4) 订阅全局 compartment：感知其它进程中 OPENCLOSE 变化并刷新本地显示
        ITfCompartmentMgr* pcm = nullptr;
        if (SUCCEEDED(m_ptim->GetGlobalCompartment(&pcm)) && pcm) {
            pcm->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &m_pCompartment);
            pcm->Release();
        }
        if (m_pCompartment) {
            ITfSource* pCompSrc = nullptr;
            if (SUCCEEDED(m_pCompartment->QueryInterface(IID_ITfSource, (void**)&pCompSrc)) && pCompSrc) {
                m_pCompSink = new (std::nothrow) CCompartmentSink(this);
                if (m_pCompSink) {
                    if (FAILED(pCompSrc->AdviseSink(IID_ITfCompartmentEventSink, m_pCompSink,
                                                    &m_dwCompSinkCookie))) {
                        m_pCompSink->Release();
                        m_pCompSink = nullptr;
                        m_dwCompSinkCookie = 0;
                    }
                }
                pCompSrc->Release();
            }
        }

        // 5) 向宿主注册模式指示 UIElement（QI 不到则优雅降级）
        ITfUIElementMgr* puiem = nullptr;
        if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfUIElementMgr, (void**)&puiem)) && puiem) {
            BOOL show = FALSE;
            if (SUCCEEDED(puiem->BeginUIElement(static_cast<ITfUIElement*>(this), &show, &m_uiElId))) {
                m_fUIElShown = show;
            } else {
                m_uiElId = 0;
            }
            puiem->Release();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Deactivate() override
    {
        // 注销 UIElement
        if (m_ptim && m_uiElId) {
            ITfUIElementMgr* puiem = nullptr;
            if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfUIElementMgr, (void**)&puiem)) && puiem) {
                puiem->EndUIElement(m_uiElId);
                puiem->Release();
            }
            m_uiElId = 0;
        }
        // 摘除 compartment 汇
        if (m_pCompSink) {
            if (m_pCompartment && m_dwCompSinkCookie) {
                ITfSource* pCompSrc = nullptr;
                if (SUCCEEDED(m_pCompartment->QueryInterface(IID_ITfSource, (void**)&pCompSrc)) && pCompSrc) {
                    pCompSrc->UnadviseSink(m_dwCompSinkCookie);
                    pCompSrc->Release();
                }
            }
            m_pCompSink->Release();
            m_pCompSink = nullptr;
            m_dwCompSinkCookie = 0;
        }
        if (m_pCompartment) { m_pCompartment->Release(); m_pCompartment = nullptr; }

        // 摘除 UIElement 汇
        if (m_ptim && m_dwUIElSinkCookie) {
            ITfSource* pSrc = nullptr;
            if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfSource, (void**)&pSrc)) && pSrc) {
                pSrc->UnadviseSink(m_dwUIElSinkCookie);
                pSrc->Release();
            }
            m_dwUIElSinkCookie = 0;
        }

        // 反注册全部 Scroll Lock 保留键变体（与 PreserveKey 一一对应）
        if (m_pKeystrokeMgr) {
            static const WORD kMods[] = {
                0, TF_MOD_SHIFT, TF_MOD_CONTROL, TF_MOD_ALT, TF_MOD_LWIN, TF_MOD_RWIN
            };
            for (WORD mod : kMods) {
                TF_PRESERVEDKEY pk;
                pk.uVKey      = VK_SCROLL;
                pk.uModifiers = mod;
                m_pKeystrokeMgr->UnpreserveKey(GUID_RuIME_PreservedKey, &pk);
            }
            m_pKeystrokeMgr->Release();
            m_pKeystrokeMgr = nullptr;
        }
        if (m_modeTextHolder()) { }   // no-op，保持成员释放顺序清晰
        if (m_pModeText) { m_pModeText->Release(); m_pModeText = nullptr; }
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

    // Scroll Lock 保留键触发：切换 俄语/英文（跨进程共享状态），并广播刷新 UI
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*, REFGUID rguid, BOOL* pfEaten) override
    {
        *pfEaten = FALSE;
        if (IsEqualGUID(rguid, GUID_RuIME_PreservedKey)) {
            SetRuEnabled(!RuEnabled());

            // 【Bug 修复】把新状态写进全局 compartment OPENCLOSE —— TSF 会把值
            // 变化广播给所有进程的 compartment 汇，其它应用里的 TIP 实例据此
            // 刷新自己的模式显示（此前共享内存只改了本进程视图的标记，其它
            // 进程毫无感知，且没有任何机制通知框架更新显示）。
            if (m_pCompartment) {
                VARIANT v;
                VariantInit(&v);
                v.vt      = VT_BOOL;
                v.boolVal = RuEnabled() ? VARIANT_TRUE : VARIANT_FALSE;
                m_pCompartment->SetValue(m_tid, &v);
            }
            RefreshModeUI();
            *pfEaten = TRUE;
        }
        return S_OK;
    }

    // ---- ITfFunctionProvider（本 TIP 自己作为 provider，供宿主取回模式文本）----
    HRESULT STDMETHODCALLTYPE GetType(GUID* pguid) override
    {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_RuIME_ModeFunctionProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* pbstr) override
    {
        if (!pbstr) return E_INVALIDARG;
        *pbstr = SysAllocString(MODE_UI_DESC);
        return *pbstr ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetFunction(REFGUID, REFIID riid, IUnknown** pUnk) override
    {
        if (!pUnk) return E_INVALIDARG;
        *pUnk = nullptr;
        CModeText* pText = ModeTextObj();
        if (!pText) return E_OUTOFMEMORY;
        return pText->QueryInterface(riid, reinterpret_cast<void**>(pUnk));
    }

    // ---- ITfUIElement（MinGW 头文件声明的最小子集：描述/GUID/Show/IsShown）----
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* description) override
    {
        // 与 ITfFunctionProvider::GetDescription 签名相同，共用实现即可
        return ITfFunctionProvider::GetDescription(description);
    }
    HRESULT STDMETHODCALLTYPE GetGUID(GUID* guid) override
    {
        if (!guid) return E_INVALIDARG;
        *guid = GUID_RuIME_ModeFunctionProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Show(WINBOOL show) override
    {
        m_fUIElShown = show;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsShown(WINBOOL* show) override
    {
        if (!show) return E_INVALIDARG;
        *show = m_fUIElShown;
        return S_OK;
    }

    // ---- ITfUIElementSink（宿主回调，仅转发刷新）----
    HRESULT STDMETHODCALLTYPE BeginUIElement(DWORD id, WINBOOL* show) override
    {
        if (id == m_uiElId && show) {
            *show = TRUE;
            RefreshModeUI();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD id) override
    {
        if (id == m_uiElId) RefreshModeUI();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EndUIElement(DWORD) override { return S_OK; }

    // 模式文本对象（懒创建；CCompartmentSink::OnChange 也经公开入口调用）
    CModeText* ModeTextObj()
    {
        if (!m_pModeText) {
            CModeText* p = new (std::nothrow) CModeText();
            if (p) {
                ITfCandidateList* pl = nullptr;
                if (SUCCEEDED(p->QueryInterface(IID_ITfCandidateList, (void**)&pl)) && pl) {
                    m_pModeText = p;          // 对象由 unique_ptr 语义手动管理：见成员注释
                } else if (pl) {
                    pl->Release();
                }
                p->Release();                 // QI 已持有一份引用；本地裸指针不再持有
            }
        }
        return m_pModeText;
    }

    // 刷新模式指示：重建文本 + 通知宿主 UIElement 管线更新
    void RefreshModeUI()
    {
        CModeText* pText = ModeTextObj();
        if (pText) {
            pText->RebuildIfStale();
            pText->ClearDirty();
        }
        if (m_ptim && m_uiElId) {
            ITfUIElementMgr* puiem = nullptr;
            if (SUCCEEDED(m_ptim->QueryInterface(IID_ITfUIElementMgr, (void**)&puiem)) && puiem) {
                puiem->UpdateUIElement(m_uiElId);
                puiem->Release();
            }
        }
    }

private:
    // QI 辅助：按请求的 IID 返回正确的接口指针（处理多重继承偏移）
    void* NormPtr(REFIID riid)
    {
        if (IsEqualGUID(riid, IID_ITfFunctionProvider)) return static_cast<ITfFunctionProvider*>(this);
        if (IsEqualGUID(riid, IID_ITfUIElement))        return static_cast<ITfUIElement*>(this);
        if (IsEqualGUID(riid, IID_ITfUIElementSink))    return static_cast<ITfUIElementSink*>(this);
        if (IsEqualGUID(riid, IID_ITfKeyEventSink))     return static_cast<ITfKeyEventSink*>(this);
        return static_cast<ITfTextInputProcessor*>(this);   // IUnknown / ITfTextInputProcessor
    }
    HRESULT FinishQI(void** ppv, REFIID riid)
    {
        *ppv = NormPtr(riid);
        AddRef();
        return S_OK;
    }
    void* modeTextHolder() { return m_pModeText; }

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

    LONG              m_cRef;
    ITfThreadMgr*     m_ptim;
    TfClientId        m_tid;
    DWORD             m_dwKeySinkCookie;
    ITfKeystrokeMgr*  m_pKeystrokeMgr;
    CModeText*        m_pModeText;       // 强引用（new 出来的对象所有权 + QI 引用合一，析构时 Release）
    ITfCompartment*   m_pCompartment;
    CCompartmentSink* m_pCompSink;
    DWORD             m_dwCompSinkCookie;
    DWORD             m_dwUIElSinkCookie;
    DWORD             m_uiElId;
    WINBOOL           m_fUIElShown;
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
