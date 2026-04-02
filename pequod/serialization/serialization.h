//
// Created by smadhur on 4/1/26.
//

#ifndef PEQUOD_SERIALIZATION_H_
#define PEQUOD_SERIALIZATION_H_
#include <tinyxml2.h>
#include <pobject/pobject.h>

using tinyxml2::XMLDocument;

namespace Pequod {
    namespace Serialization {
        XMLDocument Marshal();

        std::shared_ptr<PObject> Unmarshal();
    }
}

#endif //
