#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#define LINE_MAX_LENGTH    160
#define INVALID_SYNTAX       1
#define NO_HISTORY           2
#define OPEN_FILE_FAILED     4
#define PIPE_FAILED          8
#define FORK_FAILED         16
#define EXIT                32

FILE* logfile;

typedef struct exp_node{ //语法树节点; l, r记录参数字符串的位置; operand_num 记录子节点数目; operands 记录子节点指针; his 已弃用 
    int                      l;
    int                      r;
    int            operand_num;
    char*                  arg;
    struct exp_node** operands;
    int                    his;
};

struct exp_node* new_node(int l, int r, int operand_num, char* arg){ //创建一个初始化的语法树节点
    struct exp_node* nd = (struct exp_node*)malloc(sizeof(struct exp_node));
    nd->l = l;
    nd->r = r;
    nd->operand_num = operand_num;
    if (operand_num){
        nd->operands = (struct exp_node**)malloc(sizeof(struct exp_node*) * operand_num);
    }
    else{
        nd->operands = NULL;
    }
    for(int i = 0; i < nd->operand_num; ++i){
        nd->operands[i] = NULL;
    }
    nd->arg = arg;
    nd->his = 0;
    return nd;
}

int cmd_split(char** args, char* cmd){ //字符串命令切分 以空格为界
    int             l = 0;
    int             r = 0;
    int len = strlen(cmd);
    int       arg_num = 0;
    char              tmp;
    while (r < len - 1){
        while (cmd[l] == ' '){
            ++l;
        }
        r = l;
        while (cmd[r] != ' ' && cmd[r] != '\n'){
            ++r;
        }
        if(r > l){
            tmp = cmd[r];  
            cmd[r] = '\0';
            args[arg_num] = (char*)malloc(sizeof(char) * (r-l+1));
            strcpy(args[arg_num], cmd+l);
            args[arg_num++][r-l] = '\0';
            cmd[r] = tmp;
        }
        else{
            break;
        }
        l = r;
    }
    return arg_num;
}

struct exp_node* args_parse(char** args, int len, int* state, struct exp_node* his_rt){ //参数解析，生成语法树，返回根节点
    int                 i = 0;
    char*                 arg;
    struct exp_node*       rt;
    struct exp_node*      tmp;
    struct exp_node**     cur;
    rt = NULL;
    cur = &rt;
    *state = 0;
    while (i < len){
        arg = args[i];
        if(arg == NULL){
            *state |= INVALID_SYNTAX;
            return NULL;
        }
        else if (!strcmp(arg, "&")){
            tmp = new_node(i, i, 1, arg);
            tmp->operands[0] = rt;
            rt = tmp;
            cur = &(rt->operands[0]);
        }
        else if (!strcmp(arg, "|") || !strcmp(arg, ">") || !strcmp(arg, "<")){
            tmp = new_node(i, i, 2, arg);
            tmp->operands[0] = rt;
            rt = tmp;
            cur = &(rt->operands[1]);
        }
        else if(!strcmp(arg, "!!")){
            if (*cur == NULL){
                if(his_rt != NULL){
                    tmp = new_node(i, i, 1, arg);
                    tmp->operands[0] = his_rt;
                    *cur = tmp;
                }
                else{
                    *state |= NO_HISTORY;
                    return NULL;
                }
            }
            else{
                *state |= INVALID_SYNTAX;
                return NULL;
            }
        }
        else{
            if (*cur == NULL){
                *cur = new_node(i, i, 0, arg);
            }
            else{
                (*cur)->r = i;
            }
        }
        ++i;
    }
    return rt;
}

typedef struct exec_info{ //命令执行时信息
    int wait_flag;
    int     fd_in;
    int    fd_out;
};

int exec_exp(struct exp_node* exp, struct exec_info fa_info, char** args, char** his_args){ //执行命令
    if (exp == NULL){
        return INVALID_SYNTAX;
    }
    int             state;
    int        pipe_fd[2];
    struct exec_info info;
    info = fa_info;
    if (!strcmp(exp->arg, "&")){
        info.wait_flag = 0;
        return exec_exp(exp->operands[0], info, args, his_args);
    }
    else if (!strcmp(exp->arg, "!!")){
        return exec_exp(exp->operands[0], info, args, his_args);
    }
    else if (!strcmp(exp->arg, ">")){
        if (exp->operands[1]->r != exp->operands[1]->l){
            return INVALID_SYNTAX;
        }
        if ((info.fd_out = open(exp->operands[1]->his ? his_args[exp->operands[1]->l] : args[exp->operands[1]->l], O_RDWR|O_CREAT, 0644)) == -1){
            return OPEN_FILE_FAILED;
        }
        return exec_exp(exp->operands[0], info, args, his_args);
    }
    else if (!strcmp(exp->arg, "<")){
        if (exp->operands[1]->r != exp->operands[1]->l){
            return INVALID_SYNTAX;
        }
        if ((info.fd_in = open(exp->operands[1]->his ? his_args[exp->operands[1]->l] : args[exp->operands[1]->l], O_RDWR, 0644)) == -1){
            return OPEN_FILE_FAILED;
        }
        return exec_exp(exp->operands[0], info, args, his_args);
    }
    else if (!strcmp(exp->arg, "|")){
        //printf("Left : %s ; Right %s\n", exp->operands[0]->arg, exp->operands[1]->arg);
        fprintf(logfile, "Left : %s ; Right %s ; fd_in %d ; fd_out %d;\n", exp->operands[0]->arg, exp->operands[1]->arg, info.fd_in, info.fd_out);
        fflush(logfile);
        if (pipe(pipe_fd) == -1){
            return PIPE_FAILED;
        }
        info.fd_out = pipe_fd[1];
        state = exec_exp(exp->operands[0], info, args, his_args);
        close(pipe_fd[1]);
        if(state){
            return state;
        }
        info.fd_in = pipe_fd[0];
        info.fd_out = fa_info.fd_out;
        state = exec_exp(exp->operands[1], info, args, his_args);
        close(pipe_fd[0]);
        return state;
    }
    else{
        int l = exp->l;
        int r = exp->r;
        char** arglist = (char**)malloc(sizeof(char*) * (r - l + 2));
        char* tmparg;
        char* ccmd = exp->his ? his_args[l] : args[l];
        arglist[0] = (char*)malloc(sizeof(char) * strlen(ccmd));
        strcpy(arglist[0], ccmd);
        for(int i = 0; i < r - l; ++i){
            tmparg = exp->his ? his_args[l+1+i] : args[l+1+i];
            arglist[i+1] = (char*)malloc(sizeof(char) * strlen(tmparg));
            strcpy(arglist[i+1], tmparg);
        }
        arglist[r-l+1] = NULL;
        pid_t pid;
        pid = fork();
        if (pid < 0){
            return FORK_FAILED;
        }
        if (pid > 0){
            if (info.wait_flag){
                waitpid(pid, NULL, 0);
            }
            if(info.fd_in != -1){
                close(info.fd_in);
            }
            if(info.fd_out != -1){
                close(info.fd_out);
            }
            return 0;
        }
        else{
            fprintf(logfile, "%d %d %s\n", info.fd_in, info.fd_out, ccmd);
            for(int i = 0; arglist[i] != NULL; ++i){
                fprintf(logfile, "%s ", arglist[i]);
            }
            fprintf(logfile, "\n");
            fflush(logfile);
            //fflush(stdout);
            if (info.fd_in != -1){
                dup2(info.fd_in, STDIN_FILENO);
            }
            if (info.fd_out != -1){
                dup2(info.fd_out, STDOUT_FILENO);
            }
            execvp(ccmd, arglist);
        }
    }
}

void free_exp_tree(struct exp_node* rt){ // 已弃用
    for(int i = 0; i < rt->operand_num; ++i){
        if (rt->operands[i] != NULL){
            free_exp_tree(rt->operands[i]);
        }
    }
    rt->arg = NULL;
    free(rt);
}

void mark_his_exp_tree(struct exp_node* rt){ //已弃用
    for(int i = 0; i < rt->operand_num; ++i){
        if (rt->operands[i] != NULL){
            mark_his_exp_tree(rt->operands[i]);
        }
    }
    rt->his = 1;
}

void deal_with_state(int s){ //处理异常情况
    /*
#define INVALID_SYNTAX       1
#define NO_HISTORY           2
#define OPEN_FILE_FAILED     4
#define PIPE_FAILED          8
#define FORK_FAILED         16
#define EXIT                32
    */
    if (s & 1){
        printf("Invalid syntax. ");
    }
    if ((s >> 1) & 1){
        printf("No history. ");
    }
    if ((s >> 2) & 1){
        printf("Open file failed. ");
    }
    if ((s >> 3) & 1){
        printf("Pipe failed. ");
    }
    if ((s >> 4) & 1){
        printf("Fork failed. ");
    }
    printf("\n");
    fflush(stdout);
}

int replace_history(char** args, char** his_args, int* arg_num, int his_arg_num){ //支持历史记录替换
    int cnt = 0;
    for (int i = 0; i < *arg_num; ++i){
        if (!strcmp(args[i], "!!")){
            ++cnt;
        }
    }
    if (cnt){
        char** tmp_args = (char**)malloc(sizeof(char*) * ((*arg_num) + cnt * (his_arg_num - 1)));
        int pos = 0;
        for(int i = 0; i < *arg_num; ++i){
            if (strcmp(args[i], "!!")){
                tmp_args[pos++] = args[i];
            }
            else{
                for(int j = 0; j < his_arg_num; ++j){
                    tmp_args[pos++] = his_args[j];
                }
            }
        }
        for(int i = 0; i < pos; ++i){
            args[i] = tmp_args[i];
        }
        free(tmp_args);
        *arg_num = pos;
        return 1;
    }
    else{
        return 0;
    }
}

int main(){
    int             should_run = 1;
    char*                      cmd;
    char**                    args;
    char**                his_args;
    int                 cmd_length;
    int                    arg_num;
    int            his_arg_num = 0;
    struct exp_node*        exp_rt;
    struct exp_node*        his_rt;
    int                      state;
    struct exec_info          info;
    
    logfile = fopen("./log.txt", "w");

    info.wait_flag = 1;
    info.fd_in = -1;
    info.fd_out = -1;
    cmd = (char*)malloc(sizeof(char) * LINE_MAX_LENGTH);
    args = (char**)malloc(sizeof(char*) * ((LINE_MAX_LENGTH >> 1) + 1));
    his_args = (char**)malloc(sizeof(char*) * ((LINE_MAX_LENGTH >> 1) + 1));
    exp_rt = his_rt = NULL;
    
    for(int i = 0; i <= (LINE_MAX_LENGTH >> 1); ++i){
        args[i] = his_args[i] = NULL;
    }
    
    while (should_run){ //shell主体
        printf("osh>");
        fflush(stdout);
        fgets(cmd, LINE_MAX_LENGTH, stdin);
        cmd_length = strlen(cmd);
        fflush(stdout);
        if (cmd_length && cmd[0] != '\n'){
            arg_num = cmd_split(args, cmd);
            if(arg_num == 1 && !strcmp(args[0], "exit")){
                should_run = 0;
                break;
            }
            if (his_arg_num && replace_history(args, his_args, &arg_num, his_arg_num)){
                for(int i = 0; i < arg_num; ++i){
                    printf("%s ", args[i]);
                }
                printf("\n");
                fflush(stdout);
            }
            exp_rt = args_parse(args, arg_num, &state, his_rt);
            if(state){
                deal_with_state(state);
            }
            else{
                state = exec_exp(exp_rt, info, args, his_args);
                if (state){
                    deal_with_state(state);
                }
                for(int i = 0; i < arg_num; ++i){
                    his_args[i] = args[i];
                    args[i] = NULL;
                }
                his_arg_num = arg_num;
            }
        }
    }
    
    fclose(logfile);

    return 0;
}