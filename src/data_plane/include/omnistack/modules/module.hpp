//
// Created by liuhao on 23-5-30.
//

#ifndef OMNISTACK_MODULE_HPP
#define OMNISTACK_MODULE_HPP

#include <functional>

class Module {
public:
    static bool default_filter() {return true;}
    static std::function<bool()> get_filter() {return default_filter};
};

#endif //OMNISTACK_MODULE_HPP
