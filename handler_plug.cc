#include "handler_plug.hh"

static std::list<my_data> fnc_list;
static std::list<tree> handlers;
static std::list<handler_in_var> possible_handlers;
static std::list<handler_setter> own_setters;

static bool dependencies_handled=true;
static bool added_new_setter=false;

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
         bool fatal;
         if (scan_own_function(get_name(depends.fnc),obj.not_safe,fatal))
         {
            if (obj.not_safe)
            {
               obj.fatal=fatal;
               if (obj.is_handler)
                  print_warning(depends.handler,depends.fnc,depends.loc,fatal);
               break;
            }
            obj.depends.pop_front();
         }
         else
            solved=false;
      }
   }
}

//returns true if fnc is already a setter
bool is_setter(tree fnc)
{
   for (handler_setter &obj: own_setters)
   {
      if (strcmp(obj.setter,get_name(fnc)) == 0)
      {
         return true;
      }
   }
   return false;
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
            handler_in_var var_handler = *it;
            possible_handlers.erase(it);
            if(!is_gimple_addressable (var_handler.handler) && TREE_CODE(var) == ADDR_EXPR)
               return var_handler.handler;
            else if (TREE_CODE(var_handler.handler) == PARM_DECL)
            {
               unsigned counter=0;
               for (tree argument = DECL_ARGUMENTS (current_function_decl) ; argument ; argument = TREE_CHAIN (argument))
               {
                  if (strcmp(get_name(argument),get_name(var_handler.handler))==0)
                  {
                     handler_setter new_setter;
                     new_setter.setter = get_name(current_function_decl);
                     new_setter.position = counter;
                     own_setters.push_front(new_setter);
                     added_new_setter=true;
                     break;
                  }
                  ++counter;
               }
            }
            return nullptr;
         }
      }
      if (TREE_CODE(var) == ADDR_EXPR)
      {
         tree vardecl = TREE_OPERAND (var, 0);
         if (TREE_CODE(vardecl)==VAR_DECL && !DECL_EXTERNAL (vardecl))
         {
            tree initial = DECL_INITIAL(vardecl);
            if (initial)
            {
               initial = give_me_handler(initial,true);
               if(!is_gimple_addressable (initial) && TREE_CODE(var) == ADDR_EXPR)
                  return initial;
            }
         }
      }
   }
   else if (strcmp(name,"signal")==0 || strcmp(name,"bsd_signal")==0 || strcmp(name,"sysv_signal")==0)
   {
      tree var = gimple_call_arg (stmt, 1);
      if(!is_gimple_addressable (var) && TREE_CODE(var) == ADDR_EXPR)
         return var;
      else if (TREE_CODE(var) == PARM_DECL)
      {
         unsigned counter=0;
         for (tree argument = DECL_ARGUMENTS (current_function_decl) ; argument ; argument = TREE_CHAIN (argument))
         {
            if (strcmp(get_name(argument),get_name(var))==0)
            {
               handler_setter new_setter;
               new_setter.setter = get_name(current_function_decl);
               new_setter.position = counter;
               own_setters.push_front(new_setter);
               added_new_setter=true;
               break;
            }
            ++counter;
         }
      }
   }
   else
      return scan_own_handler_setter(stmt,current_function_decl);
   return nullptr;
}

//look through own setters, try to find handler
tree scan_own_handler_setter(gimple* stmt,tree fun_decl)
{
   for (handler_setter &obj: own_setters)
   {
      tree current_fn = gimple_call_fn(stmt);
      if (!current_fn)
         return nullptr;
      const char* name = get_name(current_fn);
      if (!name)
         return nullptr;
      if (strcmp(name,obj.setter)==0)
      {
         tree var = gimple_call_arg (stmt, obj.position);
         if(!is_gimple_addressable (var) && TREE_CODE(var) == ADDR_EXPR)
            return var;
         else if (TREE_CODE(var) == PARM_DECL)
         {
            unsigned counter=0;
            for (tree argument = DECL_ARGUMENTS (fun_decl) ; argument ; argument = TREE_CHAIN (argument))
            {
               if (strcmp(get_name(argument),get_name(var))==0)
               {
                  if (is_setter(fun_decl))
                  {
                     break;
                  }
                  handler_setter new_setter;
                  new_setter.setter = get_name(fun_decl);
                  new_setter.position = counter;
                  own_setters.push_front(new_setter);
                  added_new_setter=true;
                  break;
               }
               ++counter;
            }
         }
      }
   }
   return nullptr;
}

//try to find signal handler in the initialization of variable
tree give_me_handler(tree var,bool first)
{
   if (TREE_CODE(var)==CONSTRUCTOR)
   {
      unsigned HOST_WIDE_INT ix;
      tree field, val;

      FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (var), ix, field, val)
      {
         if (field)
         {
            if(first && strcmp(get_name(field),"__sigaction_handler")==0)
            {
               if (val)
               {
                  return give_me_handler(val,false);
               }
            }
            else if (!first && (strcmp(get_name(field),"sa_handler")==0 || strcmp(get_name(field),"sa_sigaction")==0))
            {
               return val;
            }
         }
      }
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

//scan if the called function in signal handler is asynchronous-unsafe
bool is_handler_wrong_fnc(const char* name)
{
   if (!name)
      return false;
   static const char* unsafe_fnc[]={
      "malloc", "free", "grantpt", "unlockpt", "ptsname", "ptsname_r",
      "mallinfo", "mallopt", "mtrace", "muntrace", "realloc", "reallocarray",
      "aligned_alloc", "memalign", "posix_memalign", "valloc", "calloc",
      "shm_open", "shm_unlink", "printf", "wprintf", "fprintf", "fwprintf",
      "sprintf", "swprintf", "snprintf", "sem_open", "sem_close", "sem_unlink",
      "fclose", "fcloseall", "fopen", "fopen64", "freopen", "freopen64",
      "fgetc", "fgetwc", "fgetc_unlocked", "fgetwc_unlocked", "getc",
      "getwc", "getc_unlocked", "getwc_unlocked", "getchar", "getwchar",
      "getchar_unlocked", "getwchar_unlocked", "getw", "fputc", "fputwc",
      "fputc_unlocked", "fputwc_unlocked", "putc", "putwc", "putc_unlocked",
      "putwc_unlocked", "putchar", "putwchar", "putchar_unlocked",
      "putwchar_unlocked", "fputs", "fputws", "fputs_unlocked",
      "fputws_unlocked", "puts", "putw", "strerror", "strerror_r",
      "perror", "error", "error_at_line", "warn", "vwarn", "warnx",
      "vwarnx", "err", "verr", "errx", "verrx", "scanf", "wscanf",
      "fscanf", "fwscanf", "sscanf", "swscanf", "exit", "longjmp",
      "sigsetjmp", "siglongjmp", "tmpfile", "tmpfile64", "tmpnam",
      "tempnam", "atexit", "on_exit","__builtin_putchar", "__builtin_puts"};
   for(unsigned i=0;i<(sizeof(unsafe_fnc)/sizeof(char*));++i)
   {
      if(strcmp(unsafe_fnc[i],name)==0)
         return true;
   }
   return false;
}

//scan user declared function in signal handler
bool scan_own_function (const char* name,bool &not_safe,bool &fatal)
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
            fatal=obj.fatal;
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
                     depend_data save_dependencies;
                     save_dependencies.loc = gimple_location(stmt);
                     save_dependencies.fnc = fn_decl;
                     // in case of recurse, do nothing
                     if (strcmp(get_name(obj.fnc_tree),called_function_name)==0)
                        ;
                     else if (scan_own_function(called_function_name,obj.not_safe,fatal))
                     {
                        if (obj.not_safe)
                        {
                           obj.err_loc = gimple_location(stmt);
                           obj.err_fnc=fn_decl;
                           not_safe=true;
                           obj.fatal=fatal;
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
                        if (is_handler_wrong_fnc(called_function_name))
                        {
                           obj.err_loc = gimple_location(stmt);
                           obj.err_fnc=fn_decl;
                           obj.not_safe=true;
                           not_safe=true;
                           obj.fatal=true;
                           fatal=true;
                           return true;
                        }
                        else
                        {
                           obj.err_loc = gimple_location(stmt);
                           obj.err_fnc=fn_decl;
                           obj.not_safe=true;
                           not_safe=true;
                           obj.fatal=false;
                           fatal=false;
                           return true;
                        }
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
void print_warning(tree handler,tree fnc,location_t loc,bool fatal)
{
   const char* handler_name = get_name(handler);
   const char* fnc_name = get_name(fnc);
   std::string msg = "asynchronous-unsafe function";
   if (!fatal)
      msg = "possible " + msg;
   if(!isatty(STDERR_FILENO))
   {
      msg += " ‘";
      msg += fnc_name;
      msg += "‘ in signal handler";
      msg += " ‘";
      msg += handler_name;
      msg += "‘";
   }
   else
   {
      msg += " ‘\033[1;1m";
      msg += fnc_name;
      msg += "\033[0m‘ in signal handler";
      msg += " ‘\033[1;1m";
      msg += handler_name;
      msg += "\033[0m‘";
   }
   warning_at(loc,0,msg.c_str());
}


// We must assert that this plugin is GPL compatible
int plugin_is_GPL_compatible;

static struct plugin_info handler_check_gcc_plugin_info =
{ "1.0", "This plugin scans signal handlers for asynchronous-unsafe functions" };

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
      my_data new_fnc;
      new_fnc.fun=fun;
      new_fnc.fnc_tree=current_function_decl;
      fnc_list.push_front(new_fnc);
      //std::cerr << "in function " << get_name(new_fnc.fnc_tree) << "\n";
      FOR_ALL_BB_FN(bb, fun)
      {

         gimple_stmt_iterator gsi;
         //try to identify signal handler
         for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
         {
            gimple * stmt = gsi_stmt (gsi);
            //print_gimple_stmt (stderr,stmt,0,0);
            if (gimple_code(stmt)==GIMPLE_CALL)
               handler=get_handler(stmt);
            else if (gimple_code(stmt)==GIMPLE_ASSIGN)
            {
               //expand COMPONENT_REF into separated strings
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
                                 handler_in_var new_var_handler;
                                 new_var_handler.var_name=var_name;
                                 new_var_handler.handler=gimple_assign_rhs1 (stmt);
                                 possible_handlers.push_front(new_var_handler);
                              }
                           }
                        }
                     }
                  }
               }
            }
            if (handler!=nullptr)
            {
               //std::cerr << "‘\033[1;1m signal handler " << get_name(handler) << " found \033[0m‘\n";
               //std::cerr << get_tree_code_name (TREE_CODE (handler)) << "\n";
               handlers.push_front(handler);
               handler=nullptr;
            }
         }
      }
      //if new setter was found, check already checked functions with new setters
      
      while (added_new_setter)
      {
         added_new_setter=false;
         for (my_data &obj: fnc_list)
         {
            basic_block bb;
            FOR_ALL_BB_FN(bb, obj.fun)
            {

               gimple_stmt_iterator gsi;
               for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
               {
                  gimple * stmt = gsi_stmt (gsi);
                  if (gimple_code(stmt)==GIMPLE_CALL)
                  {
                     handler = scan_own_handler_setter(stmt,obj.fnc_tree);
                     if (handler!=nullptr)
                     {
                        //std::cerr << "‘\033[1;1m signal handler " << get_name(handler) << " found \033[0m‘\n";
                        handlers.push_front(handler);
                        handler=nullptr;
                     }
                  }
               }
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
                     print_warning(handler,obj.err_fnc,obj.err_loc,true);
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
                              depend_data save_dependencies;
                              save_dependencies.loc = (gimple_location(stmt));
                              save_dependencies.fnc = fn_decl;
                              save_dependencies.handler=handler;
                              bool fatal;
                              //in case of recurse do nothing
                              if (strcmp(get_name(obj.fnc_tree),name)==0)
                                 ;
                              else if (scan_own_function(name,obj.not_safe,fatal))
                              {
                                 if (obj.not_safe)
                                 {
                                    obj.fatal=fatal;
                                    print_warning(handler,fn_decl,gimple_location(stmt),fatal);
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
                                 if (is_handler_wrong_fnc(name))
                                 {
                                    obj.not_safe=true;
                                    print_warning(handler,fn_decl,gimple_location(stmt),true);
                                    all_ok=false;
                                    obj.fatal=true;
                                    break;
                                 }
                                 else
                                 {
                                    obj.not_safe=true;
                                    print_warning(handler,fn_decl,gimple_location(stmt),false);
                                    all_ok=false;
                                    obj.fatal=false;
                                    break;
                                 }
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
