#pragma once
#include <memory>
namespace udlss {
template<class T> bool snapshotLeaseAvailable(const std::shared_ptr<T>& lease){
 return !lease || lease.use_count()==1;
}
}
