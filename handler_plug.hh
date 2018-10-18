#include <iostream>
#include <list>
#include <set>

#include "gcc-plugin.h"
#include "plugin-version.h"

#include "cp/cp-tree.h"
#include "context.h"
#include "gimple.h"
#include "gimple-predict.h"
#include "tree-pass.h"
#include "tree-ssa-operands.h"
#include "gimple-iterator.h"
#include "tree-pretty-print.h"
#include "gimple-pretty-print.h"


#define RC_NOT_ASYNCH_SAFE 0
#define RC_ASYNCH_SAFE     1
#define RC_ERRNO_CHANGED   2
#define RC_SAFE_EXIT       4
#define RC_ERRNO_SETTER    8
#define RC_ASYNCH_UNSAFE   -1

enum instruction_code {
	IC_CHANGE_ERRNO,
	IC_SAVE_ERRNO,
	IC_SAVE_FROM_VAR,
	IC_DESTROY_STORAGE,
	IC_RESTORE_ERRNO,
   IC_SET_FROM_PARM,
	IC_EXIT,
	IC_DEPEND
};

struct instruction {
	instruction_code ic;
	tree var=nullptr;
	tree from_var=nullptr;
   unsigned int param_pos=0;
	location_t instr_loc;
};

// struct for remembering dependencies across functions
struct depend_data {
  tree fnc;
  location_t loc;
  gimple * stmt;
  unsigned int parent_block_id;
  unsigned int parent_instr_loc;
};

//struct for remembering assigned functions to sigaction struct variables
struct handler_in_var {
    const char* var_name;
    tree handler;
};

struct setter_function {
    const char* setter;
    unsigned int position;
};

struct remember_error {
	location_t err_loc;
	tree err_fnc;
	bool err_fatal=false;
};

struct errno_var {
	unsigned int id;
	const char *name;
};

//TODO better name
struct errno_in_builtin {
   tree var=nullptr;
   unsigned int id;
   bool valid=false;
};

struct bb_data {
	unsigned int block_id;
	bool computed=false;
	bool is_exit=false;
	std::list<instruction> instr_list;
	std::set<errno_var> input_set;
	std::set<errno_var> output_set;
};

struct bb_link {
	unsigned int predecessor;
	unsigned int successor;
};

//struct for storing all informations about scaned functions
struct my_data {
    function* fun;
    tree fnc_tree;
    bool scaned=false;
    bool is_handler=false;
    bool is_ok=false;
    bool not_safe=false;
    bool was_err=false;
    bool fatal=false; 
    bool is_exit=false;
    bool can_be_setter=true;
    bool is_errno_setter=false;
    std::list<remember_error> err_log;
    std::list<depend_data> depends;
    
    bool errno_changed=false;
    location_t errno_loc;
    std::list<tree> stored_errno;
    
    std::list<bb_data> block_status;
    std::list<bb_link> block_links;
    
};
void handle_dependencies();
int8_t is_handler_ok_fnc (const char* name);
bool is_handler_wrong_fnc(const char* name);
void process_gimple_call(my_data &obj,bb_data &status,gimple * stmt, bool &all_ok, std::list<const char*> &call_tree,
                           bool &errno_valid, unsigned int &errno_stored, std::list<const char*> &errno_ptr);
void process_gimple_assign(my_data &obj, bb_data &status, gimple * stmt, bool &errno_valid,
                           unsigned int &errno_stored, errno_in_builtin &errno_builtin_storage, std::list<const char*> &errno_ptr);
int8_t scan_own_function (const char* name,std::list<const char*> &call_tree,bool *handler_found);
tree get_var_from_setter_stmt (gimple*stmt);
tree give_me_handler(tree var,bool first);
//setter list
bool is_setter(tree fnc, std::list<setter_function> &setter_list);
//errno setter list
bool has_same_param(setter_function &setter);
void remove_errno_setter(setter_function &setter);
//handler setter list
tree scan_own_handler_setter(gimple* stmt, tree fun_decl);
//CFG analisys
void analyze_CFG(my_data &obj);
bool compute_bb(bb_data &status, location_t &err_loc,bool &changed,my_data &obj);
void intersection(std::set<errno_var> &destination,std::set<errno_var> &source);
bool equal_sets(std::set<errno_var> &a,std::set<errno_var> &b);
errno_var tree_to_errno_var(tree var);
//warnings
inline void print_warning(tree handler,tree fnc,location_t loc,bool fatal);
inline void print_errno_warning(tree handler,location_t loc);
//errno list operations
bool is_var_in_list(tree var, std::list<tree> &list);
void add_unique_to_list(tree var, std::list<tree> &list);
//bool operators for errno_var
bool operator<(const errno_var &a, const errno_var &b);
bool operator==(const errno_var &a, const errno_var &b);


