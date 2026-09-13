#include "runtime_importer.hpp"
#include "udlss/runtime_import_policy.hpp"

#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace udlss::controller {
namespace {

bool containsI(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    auto lower=[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));};
    for (std::size_t i=0;i+needle.size()<=haystack.size();++i) {
        bool ok=true;
        for (std::size_t j=0;j<needle.size();++j) if (lower(haystack[i+j])!=lower(needle[j])) {ok=false;break;}
        if (ok) return true;
    }
    return false;
}

bool isX64WindowsBinary(const fs::path& file, std::wstring& detail) {
    std::ifstream stream(file,std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    if (!stream.read(reinterpret_cast<char*>(&dos),sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0) {
        detail=L"Rejected because the candidate is not a valid Windows PE image.";
        return false;
    }
    stream.seekg(dos.e_lfanew,std::ios::beg);
    DWORD signature=0;
    IMAGE_FILE_HEADER header{};
    if (!stream.read(reinterpret_cast<char*>(&signature),sizeof(signature)) || signature!=IMAGE_NT_SIGNATURE ||
        !stream.read(reinterpret_cast<char*>(&header),sizeof(header))) {
        detail=L"Rejected because the candidate has an invalid PE header.";
        return false;
    }
    if (header.Machine!=IMAGE_FILE_MACHINE_AMD64) {
        detail=L"Rejected because the SDK candidate is not an x64 Windows DLL.";
        return false;
    }
    return true;
}

bool verifyNvidiaAuthenticode(const fs::path& file, std::wstring& detail) {
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct=sizeof(fileInfo);
    fileInfo.pcwszFilePath=file.c_str();

    WINTRUST_DATA trust{};
    trust.cbStruct=sizeof(trust);
    trust.dwUIChoice=WTD_UI_NONE;
    trust.fdwRevocationChecks=WTD_REVOKE_NONE;
    trust.dwUnionChoice=WTD_CHOICE_FILE;
    trust.pFile=&fileInfo;
    trust.dwStateAction=WTD_STATEACTION_VERIFY;
    trust.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status=WinVerifyTrust(nullptr,&action,&trust);
    bool nvidiaSigner=false;
    std::wstring signerName;

    if (status==ERROR_SUCCESS && trust.hWVTStateData) {
        if (auto* provider=WTHelperProvDataFromStateData(trust.hWVTStateData)) {
            if (auto* signer=WTHelperGetProvSignerFromChain(provider,0,FALSE,0)) {
                if (signer->csCertChain && signer->pasCertChain && signer->pasCertChain[0].pCert) {
                    wchar_t subject[512]{};
                    const DWORD chars=CertGetNameStringW(signer->pasCertChain[0].pCert,
                        CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,subject,static_cast<DWORD>(_countof(subject)));
                    if (chars>1) signerName.assign(subject,chars-1);
                    nvidiaSigner=containsI(signerName,L"NVIDIA");
                }
            }
        }
    }

    trust.dwStateAction=WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr,&action,&trust);

    if (status!=ERROR_SUCCESS) {
        std::wstringstream out;
        out<<L"Authenticode verification failed (0x"<<std::hex<<std::uppercase
           <<static_cast<unsigned long>(status)<<L").";
        detail=out.str();
        return false;
    }
    if (!nvidiaSigner) {
        detail=signerName.empty()?L"The DLL is signed, but the signer could not be identified as NVIDIA."
                                 :L"Rejected signer: "+signerName;
        return false;
    }
    detail=L"Verified NVIDIA Authenticode signature: "+signerName;
    return true;
}

int candidateScore(const fs::path& p) {
    std::wstring text=p.wstring();
    int score=0;
    if (containsI(text,L"x64") || containsI(text,L"x86_64") || containsI(text,L"win64")) score+=40;
    if (containsI(text,L"release") || containsI(text,L"production")) score+=15;
    if (containsI(text,L"bin")) score+=5;
    if (containsI(text,L"debug") || containsI(text,L"dev")) score-=10;
    return score;
}

std::vector<fs::path> findCandidates(const fs::path& root, std::wstring_view filename) {
    std::vector<fs::path> result;
    std::error_code ec;
    if (!fs::is_directory(root,ec)) return result;
    fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
    while (it!=end) {
        if (ec) { ec.clear(); it.increment(ec); continue; }
        const auto candidatePath=it->path();
        const auto candidateName=candidatePath.filename().wstring();
        std::error_code fileEc;
        const bool regular=it->is_regular_file(fileEc);
        if (!fileEc && regular && runtimeImportKind(candidateName)!=RuntimeImportKind::Rejected &&
            runtimeAsciiIEquals(candidateName,filename))
            result.push_back(candidatePath);
        it.increment(ec);
    }
    std::stable_sort(result.begin(),result.end(),[](const fs::path&a,const fs::path&b){return candidateScore(a)>candidateScore(b);});
    return result;
}

bool copyVerifiedCandidate(const fs::path& source,const fs::path& destination,std::wstring& detail) {
    std::wstring binaryDetail;
    if (!isX64WindowsBinary(source,binaryDetail)) {detail=binaryDetail;return false;}
    std::wstring signatureDetail;
    if (!verifyNvidiaAuthenticode(source,signatureDetail)) {detail=signatureDetail;return false;}
    std::error_code ec;
    fs::create_directories(destination.parent_path(),ec);
    if (ec) {detail=L"Could not create runtime destination: "+destination.parent_path().wstring();return false;}
    ec.clear();
    fs::copy_file(source,destination,fs::copy_options::overwrite_existing,ec);
    if (ec) {const auto msg=ec.message();detail=L"Copy failed: "+std::wstring(msg.begin(),msg.end());return false;}
    detail=signatureDetail;
    return true;
}

} // namespace

RuntimeImportResult importNvidiaRuntimeFromSdk(const std::wstring& sdkRoot,
                                               const std::wstring& runtimeDestination) {
    RuntimeImportResult out{};
    const fs::path root(sdkRoot),dest(runtimeDestination);
    std::error_code rootEc;
    if (sdkRoot.empty() || !fs::is_directory(root,rootEc) || rootEc) {
        out.summary=L"Select the extracted NVIDIA Streamline/DLSS SDK folder first.";
        return out;
    }
    if (runtimeDestination.empty()) {
        out.summary=L"The UniversalDLSS5 runtime destination is empty.";
        return out;
    }

    for (const auto& spec : runtimeImportFiles()) {
        RuntimeImportItem item{};
        item.name=std::wstring(spec.name);
        item.required=spec.kind==RuntimeImportKind::RequiredNgx;
        auto candidates=findCandidates(root,spec.name);
        if (candidates.empty()) {
            item.detail=item.required?L"Required NVIDIA runtime not found in the selected SDK tree."
                                     :L"Optional Streamline runtime not found.";
            ++out.skipped;
            out.items.push_back(std::move(item));
            continue;
        }
        bool copied=false;
        std::wstring failures;
        for (const auto& candidate : candidates) {
            std::wstring detail;
            if (copyVerifiedCandidate(candidate,dest/spec.name,detail)) {
                item.source=candidate.wstring();
                item.detail=std::move(detail);
                item.copied=true;
                copied=true;
                ++out.copied;
                break;
            }
            if (!failures.empty()) failures+=L"; ";
            failures+=candidate.filename().wstring()+L": "+detail;
        }
        if (!copied) {
            item.detail=failures.empty()?L"No acceptable NVIDIA-signed x64 candidate was found.":failures;
            ++out.skipped;
        }
        out.items.push_back(std::move(item));
    }

    std::error_code ec;
    out.requiredRuntimeReady=fs::is_regular_file(dest/L"nvngx_dlssnr.dll",ec);
    std::wstringstream summary;
    if (out.requiredRuntimeReady)
        summary<<L"NVIDIA runtime ready. Imported "<<out.copied<<L" file(s) to "<<runtimeDestination<<L".";
    else
        summary<<L"Import incomplete: nvngx_dlssnr.dll was not installed. Select the official NVIDIA SDK package containing DLSS Neural Rendering.";
    if (out.skipped) summary<<L" "<<out.skipped<<L" candidate/runtime item(s) were not imported.";
    out.summary=summary.str();
    return out;
}

} // namespace udlss::controller
