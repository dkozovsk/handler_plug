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

// struct for remembering dependencies across functions
struct depend_data {
  tree fnc;
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

struct remember_error {
	location_t err_loc;
	tree err_fnc;
	bool err_fatal=false;
};

struct errno_var {
	unsigned int id;
	const char *name;
};

enum instruction_code {
	IC_CHANGE_ERRNO,
	IC_SAVE_ERRNO,
	IC_DESTROY_STORAGE,
	IC_RESTORE_ERRNO,
	IC_RETURN,
	IC_EXIT
};

struct instruction {
	instruction_code ic;
	tree var;
	location_t instr_loc;
};

struct bb_data {
	unsigned int block_id;
	bool computed=false;
	bool is_exit=false;
	std::list<instruction> instr_list;
	std::set<errno_var> input_set;
	std::set<errno_var> output_set;
};

struct status_data {
	unsigned int block_id=0;
	location_t errno_loc;
	bool errno_changed=false;
	bool errno_stored=false;
	bool return_found=false;
	bool exit_found=false;
	std::list<unsigned int> visited;
	std::list<tree> errno_list;
};

struct bb_link {
	unsigned int predecessor;
	unsigned int successor;
};

//struct for storing all informations about scaned functions
struct my_data {
    function* fun;
    tree fnc_tree;
    bool is_handler=false;
    bool is_ok=false;
    bool not_safe=false;
    bool was_err=false;
    bool fatal=false; 
    bool is_exit=false;
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
                           unsigned int &errno_stored, std::list<const char*> &errno_ptr);
int8_t scan_own_function (const char* name,std::list<const char*> &call_tree,bool *handler_found);
tree give_me_handler(tree var,bool first);
tree scan_own_handler_setter(gimple* stmt, tree fun_decl);
//CFG analisys
void analyze_CFG(my_data &obj);
bool compute_bb(bb_data &status, location_t &err_loc,bool &changed);
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


