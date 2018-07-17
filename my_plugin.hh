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

// struct for remembering dependencies across functions
struct depend_data {
  tree fnc;
  tree handler;
  int line;
  int column;
};

//struct for remembering assigned functions to sigaction struct variables
struct handler_in_var {
    const char* var_name;
    tree handler;
};

//struct for storing all informations about scaned functions
struct my_data {
    function* fun;
    tree fnc_tree;
    expanded_location err_loc;
    tree err_fnc;
    bool is_handler=false;
    bool is_ok=false;
    bool not_safe=false;
    bool was_err=false;
    std::list<depend_data> depends;
    
};
void handle_dependencies();
bool is_handler_ok_fnc (const char* name);
bool scan_own_function (const char* name,bool &not_safe);
void print_warning(tree handler,tree fnc,int line,int collumn);
