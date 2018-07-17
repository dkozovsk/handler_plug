#include "my_plugin.hh"



std::list<my_data> fnc_list;
std::list<tree> handlers;
std::list<handler_in_var> possible_handlers;

bool dependencies_handled=true;

//scan functions called in signal handlers
void handle_dependencies()
{
  bool solved=true;
  for (my_data &obj: fnc_list)
  {
    if (obj.not_safe || obj.is_ok)
      continue;
    while(solved && !obj.depends.empty())
    {
      depend_data depends=obj.depends.front();
      if (scan_own_function(get_name(depends.fnc),obj.not_safe))
      {
        if (obj.not_safe)
        {
          if (obj.is_handler)
            print_warning(depends.handler,depends.fnc,depends.line,depends.column);
          break;
        }
        obj.depends.pop_front();
      }
      else
        solved=false;
    }
  }
}

//get tree of handler from call stmt
tree get_handler(gimple* stmt)
{
  tree current_fn = gimple_call_fn(stmt);
  if (!current_fn)
    return nullptr;
  const char* name = get_name(current_fn);
  if (!name)
    return nullptr;
  if (strcmp(name,"sigaction")==0)
  {
    tree var = gimple_call_arg (stmt, 1);
    const char* name = get_name(var);
    if (!name)
      return nullptr;
    std::list<handler_in_var>::iterator it;
    for(it = possible_handlers.begin(); it != possible_handlers.end(); ++it)
    {
      if (strcmp(it->var_name,name)==0)
      {
        handler_in_var tmp = *it;
        possible_handlers.erase(it);
        if(!is_gimple_addressable (tmp.handler))
          return tmp.handler;
        return nullptr;
      }
    }
  }
  else if (strcmp(name,"signal")==0 || strcmp(name,"bsd_signal")==0 || strcmp(name,"sysv_signal")==0)
  {
    tree var = gimple_call_arg (stmt, 1);
    if(!is_gimple_addressable (var))
      return var;
  }
  return nullptr;
}

//scan if the called function in signal handler is asynchronous-safe
bool is_handler_ok_fnc (const char* name)
{
  static const char* safe_fnc[]={
    "_Exit", "fexecve", "posix_trace_event", "sigprocmask", "_exit", "fork", "pselect", "sigqueue",
    "abort", "fstat", "pthread_kill", "sigset", "accept", "fstatat", "pthread_self", "sigsuspend",
    "access", "fsync", "pthread_sigmask", "sleep", "aio_error", "ftruncate", "raise", "sockatmark",
    "aio_return", "futimens", "read", "socket", "aio_suspend", "getegid", "readlink", "socketpair",
    "alarm", "geteuid", "readlinkat", "stat", "bind", "getgid", "recv", "symlink", "cfgetispeed",
    "getgroups", "recvfrom", "symlinkat", "cfgetospeed", "getpeername", "recvmsg", "tcdrain",
    "cfsetispeed", "getpgrp", "rename", "tcflow", "cfsetospeed", "getpid", "renameat", "tcflush",
    "chdir", "getppid", "rmdir", "tcgetattr", "chmod", "getsockname", "select", "tcgetpgrp", "chown",
    "getsockopt", "sem_post", "tcsendbreak", "clock_gettime", "getuid", "send", "tcsetattr", "close",
    "kill", "sendmsg", "tcsetpgrp", "connect", "link", "sendto", "time", "creat", "linkat", "setgid",
    "timer_getoverrun", "dup", "listen", "setpgid", "timer_gettime", "dup", "lseek", "setsid",
    "timer_settime", "execl", "lstat", "setsockopt", "times", "execle", "mkdir", "setuid", "umask",
    "execv", "mkdirat", "shutdown", "uname", "execve", "mkfifo", "sigaction", "unlink", "faccessat",
    "mkfifoat", "sigaddset", "unlinkat", "fchdir", "mknod", "sigdelset", "utime", "fchmod", "mknodat",
    "sigemptyset", "utimensat", "fchmodat", "open", "sigfillset", "utimes", "fchown", "openat",
    "sigismember", "wait", "fchownat", "pause", "signal", "waitpid", "fcntl", "pipe", "sigpause",
    "write", "fdatasync", "poll", "sigpending" };
  for(unsigned i=0;i<(sizeof(safe_fnc)/sizeof(char*));++i)
  {
    if(strcmp(safe_fnc[i],name)==0)
      return true;
  }
  return false;
}
//scan user declared function in signal handler
bool scan_own_function (const char* name,bool &not_safe)
{
  basic_block bb;
  bool all_ok=false;
  for (my_data &obj: fnc_list)
  {
    if (strcmp(get_name(obj.fnc_tree),name)==0)
    {
      if (obj.not_safe)
      {
        not_safe=true;
        return true;
      }
      if (obj.is_ok)
        return true;
      all_ok=true;
      FOR_ALL_BB_FN(bb, obj.fun)
      {
        gimple_stmt_iterator gsi;
        for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
        {
          gimple * stmt = gsi_stmt (gsi);
          if (gimple_code(stmt)==GIMPLE_CALL)
          {
            tree fn_decl = gimple_call_fndecl(stmt);
            const char* called_function_name = get_name(fn_decl);
            if (DECL_INITIAL  (fn_decl))
            {
              expanded_location exp_loc = expand_location(gimple_location(stmt));
              depend_data save_dependencies;
              save_dependencies.line = exp_loc.line;
              save_dependencies.column = exp_loc.column;
              save_dependencies.fnc = fn_decl;
              // in case of recurse, do nothing
              if (strcmp(get_name(obj.fnc_tree),called_function_name)==0)
                ;
              else if (scan_own_function(called_function_name,obj.not_safe))
              {
                if (obj.not_safe)
                {
                  obj.err_loc = expand_location(gimple_location(stmt));
                  obj.err_fnc=fn_decl;
                  not_safe=true;
                  return true;
                }
              }
              else
              {
                all_ok=false;
                dependencies_handled=false;
                obj.depends.push_front(save_dependencies);
              }
            }
            else
            {
              if(!is_handler_ok_fnc(called_function_name))
              {
                obj.err_loc = expand_location(gimple_location(stmt));
                obj.err_fnc=fn_decl;
                obj.not_safe=true;
                not_safe=true;
                return true;
              }
            }
          }
        }
      }
      // if everything scaned succefully function is asynchronous-safe
      obj.is_ok=all_ok;
    }
  }
  return all_ok;
}

//print warning about non asynchronous-safe function fnc in signal handler handler
inline
void print_warning(tree handler,tree fnc,int line,int column)
{
  const char* filename = EXPR_FILENAME (handler);
  const char* handler_name = get_name(handler);
  const char* fnc_name = get_name(fnc);
  if(!isatty(STDERR_FILENO))
  {
    std::cerr << filename << ": In function ‘" << handler_name << "‘:\n";
    std::cerr << filename << ":" << line << ":" << column 
      << ": warning: non asynchronous-safe function ‘" 
      << fnc_name <<"‘ in signal handler\n";
  }
  else
  {
    std::cerr << "\033[1;1m" << filename << ":\033[0m In function ‘\033[1;1m" << handler_name << "\033[0m‘:\n";
    std::cerr << "\033[1;1m" << filename << ":" << line << ":" << column 
      << ":\033[0m \033[35;1mwarning:\033[0m non asynchronous-safe function ‘\033[1;1m" 
      << fnc_name <<"\033[0m‘ in signal handler\n";
  }
}
















// We must assert that this plugin is GPL compatible
int plugin_is_GPL_compatible;

static struct plugin_info handler_check_gcc_plugin_info =
{ "1.0", "This plugin scans signal handlers for non asynchronous-safe functions" };

namespace {
const pass_data handler_check_pass_data =
{
  GIMPLE_PASS,
  "handler_check_pass",               /* name */
  OPTGROUP_NONE,                      /* optinfo_flags */
  TV_NONE,                            /* tv_id */
  PROP_gimple_any,                    /* properties_required */
  0,                                  /* properties_provided */
  0,                                  /* properties_destroyed */
  0,                                  /* todo_flags_start */
  0                                   /* todo_flags_finish */
};

struct handler_check_pass : gimple_opt_pass
{
  handler_check_pass(gcc::context * ctx) :
  gimple_opt_pass(handler_check_pass_data, ctx)
  {}

  virtual unsigned int execute(function * fun) override
  {
    basic_block bb;
    tree handler=nullptr;
    my_data tmp;
    tmp.fun=fun;
    tmp.fnc_tree=current_function_decl;
    fnc_list.push_front(tmp);
    FOR_ALL_BB_FN(bb, fun)
    {

      gimple_stmt_iterator gsi;
      //try to identify signal handler
      for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      {
        gimple * stmt = gsi_stmt (gsi);
        if (gimple_code(stmt)==GIMPLE_CALL)
          handler=get_handler(stmt);
        else if (gimple_code(stmt)==GIMPLE_ASSIGN)
        {
          //expand COMPONENT_REF into separated stings
          if (TREE_CODE(gimple_assign_lhs(stmt))==COMPONENT_REF)
          {
            tree node,op0,op1,op2;
            node = TREE_OPERAND (gimple_assign_lhs(stmt), 0);
            op2 = TREE_OPERAND (gimple_assign_lhs(stmt), 1);
            if (TREE_CODE(node)==COMPONENT_REF)
            {
              op0 = TREE_OPERAND (node, 0);
              op1 = TREE_OPERAND (node, 1);

              if (TREE_CODE(op1)==FIELD_DECL)
              {
                const char *field_name = identifier_to_locale (IDENTIFIER_POINTER (DECL_NAME (op1)));
                if (strcmp(field_name,"__sigaction_handler")==0)
                {
                  field_name = identifier_to_locale (IDENTIFIER_POINTER (DECL_NAME (op2)));
                  if (strcmp(field_name,"sa_handler")==0 || strcmp(field_name,"sa_sigaction")==0)
                  {
                    const char *var_name=nullptr;
                    if (TREE_CODE(op0)==VAR_DECL)
                      var_name = identifier_to_locale (IDENTIFIER_POINTER (DECL_NAME (op0)));
                    else if (TREE_CODE(op0)==MEM_REF)
                    {
                      op0 = TREE_OPERAND (op0, 0);
                      var_name = identifier_to_locale (IDENTIFIER_POINTER (DECL_NAME (op0)));
                    }
                    if (var_name!= nullptr)
                    {
                      handler_in_var tmp;
                      tmp.var_name=var_name;
                      tmp.handler=gimple_assign_rhs1 (stmt);
                      possible_handlers.push_front(tmp);
                    }
                  }
                }
              }
            }
          }
        }
        if (handler!=nullptr)
        {
          handlers.push_front(handler);
          handler=nullptr;
        }
      }
    }
    //scan all identified signal handlers
    while(!handlers.empty())
    {
      handler=handlers.front();

      if (handler != nullptr)
      {
        bool found=false;
        for (my_data &obj: fnc_list)
        {
          if (strcmp(get_name(obj.fnc_tree),get_name(handler))==0)
          {
            handlers.pop_front();
            found=true;
            if ((obj.is_handler && obj.not_safe) || obj.is_ok)
              break;
            obj.is_handler=true;
            if (obj.not_safe)
            {
              print_warning(handler,obj.err_fnc,obj.err_loc.line,obj.err_loc.column);
              break;
            }
            bool all_ok=true;
            basic_block bb;
            FOR_ALL_BB_FN(bb, obj.fun)
            {

              gimple_stmt_iterator gsi;
              for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
              {
                gimple * stmt = gsi_stmt (gsi);
                if (gimple_code(stmt)==GIMPLE_CALL)
                {
                  tree fn_decl = gimple_call_fndecl(stmt);
                  const char* name = get_name(fn_decl);
                  if (DECL_INITIAL  (fn_decl))
                  {
                    expanded_location exp_loc = expand_location(gimple_location(stmt));
                    depend_data save_dependencies;
                    save_dependencies.line = exp_loc.line;
                    save_dependencies.column = exp_loc.column;
                    save_dependencies.fnc = fn_decl;
                    save_dependencies.handler=handler;
                    if (scan_own_function(name,obj.not_safe))
                    {
                      if (obj.not_safe)
                      {
                        print_warning(handler,fn_decl,exp_loc.line,exp_loc.column);
                        all_ok=false;
                        break;
                      }
                    }
                    else
                    {
                      all_ok=false;
                      dependencies_handled=false;
                      obj.depends.push_front(save_dependencies);
                    }
                  }
                  else
                  {
                    if(!is_handler_ok_fnc(name))
                    {
                      obj.not_safe=true;
                      expanded_location exp_loc = expand_location(gimple_location(stmt));
                      print_warning(handler,fn_decl,exp_loc.line,exp_loc.column);
                      all_ok=false;
                      break;
                    }
                  }
                }
              }
            }
            // if everything scaned succefully handler is ok
            obj.is_ok=all_ok;
          }
        }
        if (!found)
          break;
      }
      else
        handlers.pop_front();
    }

    //if there are unsolved dependencies try to handle them
    if (!dependencies_handled)
      handle_dependencies();

    return 0;
  }

  virtual handler_check_pass *clone() override
  {
    // We do not clone ourselves
    return this;
  }
};
}

int plugin_init(struct plugin_name_args *plugin_info,
        struct plugin_gcc_version *version)
{
  if(!plugin_default_version_check(version, &gcc_version))
  {
    std::cerr << "This GCC plugin is for version " << GCCPLUGIN_VERSION_MAJOR
      << "." << GCCPLUGIN_VERSION_MINOR << "\n";
    return 1;
  }

  register_callback(plugin_info->base_name,
    /* event */ PLUGIN_INFO,
    /* callback */ NULL,
    /* user_data */
    &handler_check_gcc_plugin_info);

  // Register the phase right after cfg
  struct register_pass_info pass_info;

  pass_info.pass = new handler_check_pass(g);
  pass_info.reference_pass_name = "cfg";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;

  register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);


  return 0;
}
