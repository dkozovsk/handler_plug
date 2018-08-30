#include "handler_plug.hh"

static std::list<my_data> fnc_list;
static std::list<tree> handlers;
static std::list<handler_in_var> possible_handlers;
static std::list<handler_setter> own_setters;

static bool dependencies_handled=true;
static bool added_new_setter=false;

static const char * const plugin_name = "handler_plug";

//scan functions which are defined after the scan of function, which called them
void handle_dependencies() //TODO maybe extend errno check
{
   bool all_solved=true;
   for (my_data &obj: fnc_list)
   {
      bool solved=true;
      if (obj.not_safe || obj.is_ok)
         continue;
      while(solved && !obj.depends.empty())
      {
         depend_data depends=obj.depends.front();
         bool fatal=false;
         bool not_safe=false;
         std::list<const char*> call_tree;
         bool errno_changed=false;
         if (scan_own_function(get_name(depends.fnc),not_safe,fatal,errno_changed,call_tree,nullptr))
         {
            //primitive check, error only if errno was changed and was never stored
            if (errno_changed && !obj.errno_changed && !obj.was_err)
            {
               if (obj.stored_errno.empty())
               {
                  obj.errno_changed=true;
                  obj.was_err =true;
                  print_errno_warning(obj.fnc_tree,depends.loc);
               }
            }
            if (not_safe)
            {
               obj.not_safe=true;
               if (!obj.fatal)
                  obj.fatal=fatal;
               if (obj.is_handler)
                  print_warning(obj.fnc_tree,depends.fnc,depends.loc,fatal);
               else
               {
                  remember_error new_err;
                  new_err.err_loc = depends.loc;
                  new_err.err_fnc = depends.fnc;
                  new_err.err_fatal = fatal;
                  obj.err_log.push_back(new_err);
               }
            }
            obj.depends.pop_front();
         }
         else
         {
            solved=false;
            all_solved=false;
         }
      }
   }
   if (all_solved)
      dependencies_handled=true;
}

void analyze_CFG(my_data &obj)
{
   std::list<status_data> status_list;
   {
      status_data init;
      status_list.push_back(init);
   }
   while(!status_list.empty())
   {
      status_data actual_status= status_list.front();
      status_list.pop_front();
      if(actual_status.block_id==1)
      {
         if(actual_status.errno_changed)
         {
            obj.errno_loc=actual_status.errno_loc;
            obj.errno_changed=true;
            return;
         }
         continue;
      }
      bool recurse=false;
      for(unsigned int id : actual_status.visited)
      {
         if(id==actual_status.block_id)
         {
            recurse=true;
            break;
         }
      }
      if(recurse)
         continue;
      actual_status.visited.push_back(actual_status.block_id);
      for(bb_data &status : obj.block_status)
      {
         if(status.block_id==actual_status.block_id)
         {
            if(!actual_status.errno_changed)
            {
               if(!actual_status.errno_stored && status.errno_stored)
               {
                  actual_status.errno_stored=true;
                  actual_status.errno_list=status.errno_list;
               }
               else if(status.errno_stored)
               {
                  for(tree errno_var : status.errno_list)
                     actual_status.errno_list.push_back(errno_var);
               }
            }
            if(status.errno_changed)
            {
               if(!actual_status.errno_changed)
                  actual_status.errno_loc=status.errno_loc;
               actual_status.errno_changed=true;
            }
            if(status.errno_restored && actual_status.errno_stored)
            {
               for(tree errno_var : actual_status.errno_list)
               {
                  if(DECL_SOURCE_LINE (errno_var)==DECL_SOURCE_LINE (status.restore_tree))
                  {
                     const char* errno_name=get_name(errno_var);
                     const char* restore_name=get_name(status.restore_tree);
                     if(!errno_name || !restore_name)
                        continue;
                     if(strcmp(restore_name,errno_name)==0)
                        actual_status.errno_changed=false;
                  }
               }
            }
            actual_status.return_found=status.return_found;
            actual_status.exit_found=status.exit_found;
            break;
         }
      }
      if(actual_status.exit_found)
         continue;
      if(actual_status.return_found)
      {
         if(actual_status.errno_changed)
         {
            obj.errno_loc=actual_status.errno_loc;
            obj.errno_changed=true;
            return;
         }
         continue;
      }
      for(bb_link &link : obj.block_links)
      {
         if(link.predecessor==actual_status.block_id)
         {
            status_data tmp=actual_status;
            tmp.block_id=link.successor;
            status_list.push_back(tmp);
         }
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
            if(!is_gimple_addressable (var_handler.handler) && TREE_CODE(var_handler.handler) == ADDR_EXPR)
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
         if (!vardecl)
            return nullptr;
         if (TREE_CODE(vardecl)==VAR_DECL && !DECL_EXTERNAL (vardecl))
         {
            tree initial = DECL_INITIAL(vardecl);
            if (initial)
            {
               initial = give_me_handler(initial,true);
               if(!is_gimple_addressable (initial) && TREE_CODE(initial) == ADDR_EXPR)
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

//look through own setters, try to find handler if own setter was called
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
//first indicate if the function is called for the first time, second call is recursive
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

//scan if the called function in signal handler is asynchronous-safe and if errno may be changed
//returns 0 when function is not asynchronous-safe, 
//        1 if it is safe and errno is not changed,
//        2 if it is safe, but errno may be changed,
//        4 if it is safe exit function
uint8_t is_handler_ok_fnc (const char* name)
{
   if (!name)
      return 0;
   static const char* safe_fnc[]={
      "aio_error", "alarm", "cfgetispeed", "cfgetospeed", "getegid",
      "geteuid", "getgid", "getpgrp", "getpid", "getppid", "getuid",
      "posix_trace_event", "pthread_kill", "pthread_self",
      "pthread_sigmask", "raise", "sigfillset", "sleep", "umask"};
   static const char* change_errno_fnc[]={
      "accept", "access", "aio_return", "aio_suspend", "bind", "cfsetispeed", "cfsetospeed",
      "clock_gettime", "close", "connect", "creat", "dup", "dup2", "execl", "execle", "execv",
      "execve", "faccessat", "fcntl", "fdatasync", "fexecve", "fchdir", "fchmod", "fchmodat",
      "fchown", "fchownat", "fork", "fstat", "fstatat", "fsync", "ftruncate", "futimens",
      "getgroups", "getpeername", "getsockname", "getsockopt", "chdir", "chmod", "chown",
      "kill", "link", "linkat", "listen", "lseek", "lstat", "mkdir", "mkdirat", "mkfifo",
      "mkfifoat", "mknod", "mknodat", "open", "openat", "pause", "pipe", "poll", "pselect",
      "read", "readlink", "readlinkat", "recv", "recvfrom", "recvmsg", "rename", "renameat",
      "rmdir", "select", "sem_post", "send", "sendmsg", "sendto", "setgid", "setpgid", "setsid",
      "setsockopt", "setuid", "shutdown", "sigaction", "sigaddset", "sigdelset", "sigemptyset",
      "sigismember", "signal", "sigpause", "sigpending", "sigprocmask", "sigqueue", "sigset",
      "sigsuspend", "sockatmark", "socket", "socketpair", "stat", "symlink", "symlinkat", "tcdrain",
      "tcflow", "tcflush", "tcgetattr", "tcgetpgrp", "tcsendbreak", "tcsetattr", "tcsetpgrp", "time",
      "timer_getoverrun", "timer_gettime", "timer_settime", "times", "uname", "unlink", "unlinkat",
      "utime", "utimensat", "utimes", "wait", "waitpid", "write"};
   static const char* safe_exit_fnc[]={
      "abort", "_exit", "_Exit"};
   for(unsigned i=0;i<(sizeof(safe_exit_fnc)/sizeof(char*));++i)
   {
      if(strcmp(safe_exit_fnc[i],name)==0)
         return 4;
   }
   for(unsigned i=0;i<(sizeof(safe_fnc)/sizeof(char*));++i)
   {
      if(strcmp(safe_fnc[i],name)==0)
         return 1;
   }
   for(unsigned i=0;i<(sizeof(change_errno_fnc)/sizeof(char*));++i)
   {
      if(strcmp(change_errno_fnc[i],name)==0)
         return 2;
   }
   return 0;
}

//scan if the called function in signal handler is asynchronous-unsafe
bool is_handler_wrong_fnc(const char* name)
{
   if (!name)
      return false;
   static const char* unsafe_fnc[]={
      "aligned_alloc", "atexit", "__builtin_putchar", "__builtin_puts", "calloc",
      "err", "error", "error_at_line", "errx", "exit", "fclose", "fcloseall",
      "fgetc", "fgetc_unlocked", "fgetwc", "fgetwc_unlocked", "fopen", "fopen64",
      "fprintf", "fputc", "fputc_unlocked", "fputs", "fputs_unlocked", "fputwc",
      "fputwc_unlocked", "fputws", "fputws_unlocked", "free", "freopen",
      "freopen64", "fscanf", "fwprintf", "fwscanf", "getc", "getc_unlocked",
      "getchar", "getchar_unlocked", "getw", "getwc", "getwc_unlocked",
      "getwchar", "getwchar_unlocked", "grantpt", "longjmp", "mallinfo",
      "malloc", "mallopt", "memalign", "mtrace", "muntrace", "on_exit", "perror",
      "posix_memalign", "printf", "ptsname", "ptsname_r", "putc",
      "putc_unlocked", "putchar", "putchar_unlocked", "puts", "putw", "putwc",
      "putwc_unlocked", "putwchar", "putwchar_unlocked", "realloc",
      "reallocarray", "scanf", "sem_close", "sem_open", "sem_unlink",
      "shm_open", "shm_unlink", "siglongjmp", "sigsetjmp", "snprintf",
      "sprintf", "sscanf", "strerror", "strerror_r", "swprintf", "swscanf",
      "tempnam", "tmpfile", "tmpfile64", "tmpnam", "unlockpt", "valloc", "verr",
      "verrx", "vwarn", "vwarnx", "warn", "warnx", "wprintf", "wscanf"};
   for(unsigned i=0;i<(sizeof(unsafe_fnc)/sizeof(char*));++i)
   {
      if(strcmp(unsafe_fnc[i],name)==0)
         return true;
   }
   return false;
}

/* scan user declared function for asynchronous-unsafe functions
   name contains name of function, which should be scaned
   not_safe returns true to this function, if the scaned function is not asynchronous-safe
   fatal returns true to this function if the scaned function is asynchronous-unsafe
   errno_err returns true if scaned function may change errno
   call_tree is list of nested calls of functions, it is necesery for recurse
   handler_found if this pointer is set, scaned function is handler and returns there, if handler was found or not
   this function returns true, if function was scaned succesfully
*/
bool scan_own_function (const char* name, bool &not_safe, bool &fatal,
                        bool &errno_err, std::list<const char*> &call_tree,bool *handler_found) 
{
   //check for undirect recurse and should stop infinite call of this function
   for (const char* fnc: call_tree)
   {
      if (strcmp(name,fnc)==0)
         return true;
   }
   call_tree.push_back(name);
   basic_block bb;
   bool all_ok=false;
   
   bool errno_valid=false;
   unsigned int errno_stored=0;
   std::list<const char*> errno_ptr;
   
   for (my_data &obj: fnc_list)
   {
      if (strcmp(get_name(obj.fnc_tree),name)==0)
      {
         if (handler_found != nullptr)
         {
            *handler_found=true;
            if (obj.errno_changed && !obj.is_handler)
               print_errno_warning(obj.fnc_tree,obj.errno_loc);
            if (obj.is_handler &&  (obj.not_safe || obj.is_ok) )
               return true;
            obj.is_handler=true;
            if (obj.is_ok)
               return true;
            if (obj.not_safe)
            {
               while (!obj.err_log.empty())
               {
                  remember_error err = obj.err_log.front();
                  print_warning(obj.fnc_tree,err.err_fnc,err.err_loc,err.err_fatal);
                  obj.err_log.pop_front();
               }
               break;
            }
         }
         else 
         {
            if (obj.not_safe)
            {
               not_safe=true;
               fatal=obj.fatal;
               call_tree.pop_back();
               return true;
            }
            if (obj.is_ok)
            {
               call_tree.pop_back();
               return true;
            }
         }
         all_ok=true;
         FOR_ALL_BB_FN(bb, obj.fun)
         {
            bb_data status;
            status.block_id=bb->index;
            
            gimple_stmt_iterator gsi;
            for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
            {
               gimple * stmt = gsi_stmt (gsi);
               /*if (handler_found != nullptr)
               {
                  print_gimple_stmt(stderr,stmt,0,0);
                  std::cerr << gimple_code(stmt) << "\n";
                  //std::cerr << GIMPLE_RETURN << "\n";
                  //std::cerr << GIMPLE_PREDICT << "\n";
                  
               }*/
               if (gimple_code(stmt)==GIMPLE_CALL)
               {
                  tree fn_decl = gimple_call_fndecl(stmt);
                  if (!fn_decl)
                     continue;
                  const char* called_function_name = get_name(fn_decl);
                  if (!called_function_name)
                     continue;
                  uint8_t return_number = 0;
                  if (DECL_INITIAL  (fn_decl))
                  {
                     depend_data save_dependencies;
                     save_dependencies.loc = gimple_location(stmt);
                     save_dependencies.fnc = fn_decl;
                     bool fatal_call=false;
                     bool call_not_safe=false;
                     bool call_errno_changed=false;
                     // in case of recurse, do nothing
                     if (strcmp(get_name(obj.fnc_tree),called_function_name)==0)
                        ;
                     else if (scan_own_function(called_function_name,call_not_safe,fatal_call,
                                                call_errno_changed,call_tree,nullptr))
                     {
                        if (call_errno_changed)
                        {
                           if(!status.errno_changed)
                              status.errno_loc=gimple_location(stmt);
                           status.errno_changed=true;
                           status.errno_restored=false;
                        }
                        if (call_not_safe)
                        {
                           all_ok=false;
                           obj.not_safe=true;
                           
                           if (handler_found != nullptr)
                              print_warning(obj.fnc_tree,fn_decl,gimple_location(stmt),fatal_call);
                           else
                           {
                              remember_error new_err;
                              new_err.err_loc = gimple_location(stmt);
                              new_err.err_fnc = fn_decl;
                              new_err.err_fatal = fatal_call;
                              obj.err_log.push_back(new_err);
                           }
                        
                           not_safe=true;
                           if (!obj.fatal)
                              obj.fatal=fatal_call;
                           if (!fatal)
                              fatal=fatal_call;
                           continue;
                        }
                     }
                     else
                     {
                        if (call_errno_changed)
                        {
                           if(!status.errno_changed)
                              status.errno_loc=gimple_location(stmt);
                           status.errno_changed=true;
                           status.errno_restored=false;
                        }
                        all_ok=false;
                        dependencies_handled=false;
                        obj.depends.push_front(save_dependencies);
                     }
                  }
                  else
                  {
                     if(strcmp(called_function_name,"__errno_location")==0)
                     {
                        tree var = gimple_call_lhs (stmt);
                        if(TREE_CODE (var) == SSA_NAME)
                        {
                           errno_valid = true;
                           errno_stored = SSA_NAME_VERSION (var);
                        }
                        else if(TREE_CODE (var) == VAR_DECL)
                        {
                           const char* var_name = get_name(var);
                           if (var_name)
                           {
                              errno_ptr.push_front(var_name);
                           }
                        }
                     }
                     else if((return_number = is_handler_ok_fnc(called_function_name)) == 0)
                     {
                        if (is_handler_wrong_fnc(called_function_name))
                        {
                           all_ok=false;
                           
                           if (handler_found != nullptr)
                              print_warning(obj.fnc_tree,fn_decl,gimple_location(stmt),true);
                           else
                           {
                              remember_error new_err;
                              new_err.err_loc = gimple_location(stmt);
                              new_err.err_fnc = fn_decl;
                              new_err.err_fatal = true;
                              obj.err_log.push_back(new_err);
                           }
                           
                           obj.not_safe=true;
                           not_safe=true;
                           obj.fatal=true;
                           fatal=true;
                           continue;
                        }
                        else
                        {
                           all_ok=false;
                           
                           if (handler_found != nullptr)
                              print_warning(obj.fnc_tree,fn_decl,gimple_location(stmt),false);
                           else
                           {
                              remember_error new_err;
                              new_err.err_loc = gimple_location(stmt);
                              new_err.err_fnc = fn_decl;
                              new_err.err_fatal = false;
                              obj.err_log.push_back(new_err);
                           }
                           
                           obj.not_safe=true;
                           not_safe=true;
                           continue;
                        }
                     }
                     //check if errno was not changed or exit was found
                     if (return_number == 2)
                     {
                        if(!status.errno_changed)
                           status.errno_loc=gimple_location(stmt);
                        status.errno_changed=true;
                        status.errno_restored=false;
                     }
                     else if (return_number == 4)
                     {
                        status.exit_found=true;
                     }
                     
                  }
               }
               else if (!obj.was_err && gimple_code(stmt)==GIMPLE_PREDICT)
               {
                  if (gimple_predict_predictor (stmt)==PRED_TREE_EARLY_RETURN)
                  {
                     status.return_found=true;
                     /*if (obj.errno_changed && !obj.was_err)
                     {
                        obj.was_err=true;
                        if (handler_found != nullptr)
                           print_errno_warning(obj.fnc_tree,obj.errno_loc);
                     }*/
                  }                  
               }
               else if (!obj.was_err && gimple_code(stmt)==GIMPLE_ASSIGN)
               {
                  //check if errno was stored or restored
                  //TODO more possible conditions, extend errno check
                  tree r_var = gimple_assign_rhs1 (stmt);
                  tree l_var = gimple_assign_lhs (stmt);
                  if (r_var && l_var)
                  {
                     //TODO maybe stored to another place from already stored errno(rare)
                     if (TREE_CODE (l_var) == MEM_REF)
                     {
                        //get ID of mem_ref SSA_NAME variable(build in variable)
                        l_var = TREE_OPERAND (l_var, 0);
                        if (l_var)
                        {
                           if(TREE_CODE (l_var) == SSA_NAME)
                           {
                              if (errno_valid)
                              {
                                 if (errno_stored == SSA_NAME_VERSION (l_var))
                                 {
                                    status.errno_restored=false;
                                    if (TREE_CODE (r_var) == VAR_DECL)
                                    {
                                       const char* name = get_name(r_var);
                                       if (name)
                                       {
                                          for(tree errno_in_var : obj.stored_errno)
                                          {
                                             if(DECL_SOURCE_LINE (r_var)!=DECL_SOURCE_LINE (errno_in_var))
                                                continue;
                                             const char* errno_name=get_name(errno_in_var);
                                             if(!errno_name)
                                                break;
                                             if(strcmp(name,errno_name)==0)
                                             {
                                                status.errno_restored=true;
                                                status.restore_tree=r_var;
                                             }
                                          }
                                          if (!status.errno_restored)
                                          {
                                             if (!status.errno_changed)
                                             {
                                                status.errno_loc=gimple_location(stmt);
                                                status.errno_changed=true;
                                             }
                                          }
                                       }
                                    }
                                    else
                                    {
                                       if (!status.errno_changed)
                                       {
                                          status.errno_loc=gimple_location(stmt);
                                          status.errno_changed=true;
                                       }
                                       
                                    }
                                 } 
                              }
                           }
                           else if (TREE_CODE (l_var) == VAR_DECL)
                           {
                              const char* name = get_name(l_var);
                              for(const char* errno_ref : errno_ptr)
                              {
                                 if (strcmp(name,errno_ref)==0)
                                 {
                                    status.errno_restored=false;
                                    if (TREE_CODE (r_var) == VAR_DECL)
                                    {
                                       name = get_name(r_var);
                                       if (name)
                                       {
                                          for(tree errno_in_var : obj.stored_errno)
                                          {
                                             if(DECL_SOURCE_LINE (r_var)!=DECL_SOURCE_LINE (errno_in_var))
                                                continue;
                                             const char* errno_name=get_name(errno_in_var);
                                             if(!errno_name)
                                                break;
                                             if(strcmp(name,errno_name)==0)
                                             {
                                                status.errno_restored=true;
                                                status.restore_tree=r_var;
                                             }
                                          }
                                          if (!status.errno_restored)
                                          {
                                             if (!status.errno_changed)
                                             {
                                                status.errno_loc=gimple_location(stmt);
                                                status.errno_changed=true;
                                             }
                                          }
                                       }
                                    }
                                    else
                                    {
                                       if (!status.errno_changed)
                                       {
                                          status.errno_loc=gimple_location(stmt);
                                          status.errno_changed=true;
                                       }
                                    }
                                    break;
                                 }
                              }
                           }
                        }
                     }
                     else if (TREE_CODE (r_var) == MEM_REF)
                     {
                        //get ID of mem_ref SSA_NAME variable(build in variable)
                        r_var = TREE_OPERAND (r_var, 0);
                        if (r_var)
                        {
                           if(TREE_CODE (r_var) == SSA_NAME)
                           {
                              if (errno_valid && !status.errno_changed)
                              {
                                 if (errno_stored == SSA_NAME_VERSION (r_var))
                                 {
                                    if (TREE_CODE (l_var) == VAR_DECL)
                                    {
                                       status.errno_stored=true;
                                       obj.stored_errno.push_front(l_var);
                                       status.errno_list.push_back(l_var);
                                       /*const char* name = get_name(l_var);
                                       if (name)
                                       {
                                          status.errno_stored=true;
                                          obj.stored_errno.push_front(name);
                                       }*/
                                    }
                                 }
                              }
                           }
                           else if(TREE_CODE (r_var) == VAR_DECL)
                           {
                              const char* name = get_name(r_var);
                              for(const char* errno_ref : errno_ptr)
                              {
                                 if (strcmp(name,errno_ref)==0)
                                 {
                                    if(!status.errno_changed)
                                    {
                                       if (TREE_CODE (l_var) == VAR_DECL)
                                       {
                                          status.errno_stored=true;
                                          obj.stored_errno.push_front(l_var);
                                          status.errno_list.push_back(l_var);
                                          /*name = get_name(l_var);
                                          if (name)
                                          {
                                             status.errno_stored=true;
                                             obj.stored_errno.push_front(name);
                                          }*/
                                       }
                                    }
                                    break;
                                 }
                              }
                           }
                        }
                     }
                  }
               }
            }
            if (!obj.was_err)
            {
               obj.block_status.push_back(status);
               //TODO store info about control flow, more structures needed, errno info about block, not functions
               //TODO analyze control flow graph using stored informations about blocks, print error if there is way that may change errno
               edge e;
               edge_iterator ei;
          
               FOR_EACH_EDGE(e, ei, bb->succs)
               {
                 basic_block succ = e->dest;
                 bb_link link;
                 link.predecessor=bb->index;
                 link.successor=succ->index;
                 obj.block_links.push_back(link);
               }
            }
         }
         //TODO analyze control flow
         if (!obj.was_err)
         {
            analyze_CFG(obj);
            if (handler_found != nullptr && obj.errno_changed)
            {
               obj.was_err=true;
               print_errno_warning(obj.fnc_tree,obj.errno_loc);
            }
         }
         if (not_safe)
         {
            call_tree.pop_back();
            return true;
         }
         // if everything scaned succefully function is asynchronous-safe
         obj.is_ok=all_ok;
      }
   }
   call_tree.pop_back();
   return all_ok;
}

//print warning about non asynchronous-safe function 'fnc' in signal handler 'handler'
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
      msg += " [-fplugin=";
      msg += plugin_name;
      msg += "]";
   }
   else
   {
      msg += " ‘\033[1;1m";
      msg += fnc_name;
      msg += "\033[0m‘ in signal handler";
      msg += " ‘\033[1;1m";
      msg += handler_name;
      msg += "\033[0m‘";
      msg += " [\033[1;35m-fplugin=";
      msg += plugin_name;
      msg += "\033[0m]";
   }
   warning_at(loc,0,"%s",msg.c_str());
}

//print warning about changed errno in signal handler 'handler'
inline void print_errno_warning(tree handler,location_t loc)
{
   const char* handler_name = get_name(handler);
   std::string msg = "errno may be changed in signal handler";
   if(!isatty(STDERR_FILENO))
   {

      msg += " ‘";
      msg += handler_name;
      msg += "‘";
      msg += " [-fplugin=";
      msg += plugin_name;
      msg += "]";
   }
   else
   {
      msg += " ‘\033[1;1m";
      msg += handler_name;
      msg += "\033[0m‘";
      msg += " [\033[1;35m-fplugin=";
      msg += plugin_name;
      msg += "\033[0m]";
   }
   warning_at(loc,0,"%s",msg.c_str());
}



// We must assert that this plugin is GPL compatible
int plugin_is_GPL_compatible;

static struct plugin_info handler_check_gcc_plugin_info =
{ "0.1", "This plugin scans signal handlers for violations of signal-safety rules" };

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
      //Start look for handlers
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
                        if (!DECL_NAME(op1))
                           continue;
                        const char *field_name = identifier_to_locale (IDENTIFIER_POINTER (DECL_NAME (op1)));
                        if (strcmp(field_name,"__sigaction_handler")==0)
                        {
                           if (!DECL_NAME(op2))
                              continue;
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
      //End look for handlers
      
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
            bool fatal=false;
            bool not_safe=false;
            bool errno_changed=false;
            std::list<const char*> call_tree;
            scan_own_function(get_name(handler),not_safe,fatal,errno_changed,call_tree,&found);
            if (!found)
               break;
            else
               handlers.pop_front();
         }
         else
            handlers.pop_front();
      }
      //end handlers check

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
