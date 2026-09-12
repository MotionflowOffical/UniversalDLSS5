#include "backend.hpp"
#include <filesystem>
#include <string>

namespace udlss::neural {

class InGameNR final : public Backend {
public:
    ~InGameNR() override { if(inner_) destroyBackend(inner_); }

    bool initialize(const BackendInitContext& context,const std::wstring& runtime,const Settings& settings,RuntimeStatus& status) override {
        if(inner_){destroyBackend(inner_);inner_=nullptr;}
        status.neuralLocation=NeuralExecutionLocation::InGame;
        status.neuralBackendKind=NeuralBackendKind::Unknown;

        const std::filesystem::path r(runtime);
        std::error_code ec;
        const bool hasStreamline = std::filesystem::is_regular_file(r/L"sl.interposer.dll",ec) &&
                                   std::filesystem::is_regular_file(r/L"sl.common.dll",ec) &&
                                   std::filesystem::is_regular_file(r/L"sl.dlss_nr.dll",ec);
        if(hasStreamline){
            RuntimeStatus slStatus=status;
            Backend* sl=createStreamlineNR();
            if(sl && sl->initialize(context,runtime,settings,slStatus)){
                inner_=sl; status=slStatus; status.neuralLocation=NeuralExecutionLocation::InGame;
                status.neuralBackendKind=NeuralBackendKind::Streamline1004;
                wcscpy_s(status.backendName,L"Direct in-game NR (Streamline 1004)");
                return true;
            }
            if(sl) destroyBackend(sl);
        }

        RuntimeStatus ngxStatus=status;
        Backend* ngx=createNgxNR();
        if(ngx && ngx->initialize(context,runtime,settings,ngxStatus)){
            inner_=ngx; status=ngxStatus; status.neuralLocation=NeuralExecutionLocation::InGame;
            status.neuralBackendKind=NeuralBackendKind::SignedFeature18;
            wcscpy_s(status.backendName,L"Direct in-game NR (signed feature 18)");
            return true;
        }
        if(ngx) destroyBackend(ngx);
        status=ngxStatus;
        status.neuralLocation=NeuralExecutionLocation::InGame;
        status.neuralBackendKind=NeuralBackendKind::SignedFeature18;
        return false;
    }

    bool evaluate(ID3D11DeviceContext* context,const FrameResources& frame,const Settings& settings,RuntimeStatus& status) override {
        if(!inner_) return false;
        const bool ok=inner_->evaluate(context,frame,settings,status);
        status.neuralLocation=NeuralExecutionLocation::InGame;
        if(status.neuralBackendKind==NeuralBackendKind::Unknown)
            status.neuralBackendKind=std::wstring_view(inner_->name()).find(L"Streamline")!=std::wstring_view::npos ? NeuralBackendKind::Streamline1004 : NeuralBackendKind::SignedFeature18;
        return ok;
    }
    void reset() override { if(inner_) inner_->reset(); }
    const wchar_t* name() const override { return inner_?inner_->name():L"Direct in-game DLSS 5 NR"; }
private:
    Backend* inner_{};
};

Backend* createInGameNR(){ return new InGameNR; }

} // namespace udlss::neural
