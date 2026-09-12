#include "backend.hpp"
namespace udlss::neural {
class Passthrough final:public Backend{
public:
 bool initialize(const BackendInitContext&,const std::wstring&,const Settings&,RuntimeStatus&)override{return true;}
 bool evaluate(ID3D11DeviceContext* c,const FrameResources& f,const Settings&,RuntimeStatus&)override{if(!c||!f.input||!f.output)return false;c->CopyResource(f.output,f.input);return true;}
 void reset()override{}
 const wchar_t* name()const override{return L"Passthrough";}
};
Backend* createPassthrough(){return new Passthrough;} void destroyBackend(Backend*b){delete b;}
}
