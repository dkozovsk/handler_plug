#include <iostream>
#include <list>

#include "gcc-plugin.h"
#include "plugin-version.h"

#include "cp/cp-tree.h"
#include "context.h"
#include "gimple.h"
#include "tree-pass.h"
#include "tree-ssa-operands.h"
#include "gimple-iterator.h"
#include "tree-pretty-print.h"
#include "gimple-pretty-print.h"

// struct for remembering dependencies across functions
struct depend_data {
  tree fnc;
  tree handler;
  location_t loc;
};

//struct for remembering assigned functions to sigaction struct variables
struct handler_in_var {
    const char* var_name;
    tree handler;
};

struct handler_setter {
    const char* setter;
    unsigned int position;
};

//struct for storing all informations about scaned functions
struct my_data {
    function* fun;
    tree fnc_tree;
    location_t err_loc;
    tree err_fnc;
    bool is_handler=false;
    bool is_ok=false;
    bool not_safe=false;
    bool was_err=false;
    bool fatal=false;
    std::list<depend_data> depends;
    
};
void handle_dependencies();
bool is_handler_ok_fnc (const char* name);
bool is_handler_wrong_fnc(const char* name);
bool scan_own_function (const char* name,bool &not_safe,bool &fatal);
tree give_me_handler(tree var,bool first);
tree scan_own_handler_setter(gimple* stmt, tree fun_decl);
inline void print_warning(tree handler,tree fnc,location_t loc,bool fatal);
