// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"

/*increase PC*/
void IncreasePC() {
    int pcAfter = machine->ReadRegister(NextPCReg) + 4;
    machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
    machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
    machine->WriteRegister(NextPCReg, pcAfter);
}
/*add syscall handlers*/
void ExceptionHandlerHalt() {
     DEBUG('a', "\n Shutdown, initiated by user program.");
     printf("\n Shutdown, initiated by user program.");
     interrupt->Halt();
}

void ExceptionHandlerCreate() {
    int virtAddr;
    char* filename;
    DEBUG('a',"\n SC_Create call ...");
    DEBUG('a',"\n Reading virtual address of filename");
    // Lấy tham số tên tập tin từ thanh ghi r4
    virtAddr = machine->ReadRegister(4);
    DEBUG('a',"\n Reading filename.");
    // MaxFileLength là = 32
    filename = User2System(virtAddr, 32+1);
    if (filename == NULL){
        printf("\n Not enough memory in system");
        DEBUG('a',"\n Not enough memory in system");
        machine->WriteRegister(2,-1); // trả về lỗi cho chương
        // trình người dùng
        delete filename;
        return;
    }
    DEBUG('a',"\n Finish reading filename.");
    //DEBUG('a',"\n File name : '"<<filename<<"'");
    // Create file with size = 0
    // Dùng đối tượng fileSystem của lớp OpenFile để tạo file,
    // việc tạo file này là sử dụng các thủ tục tạo file của hệ điều
    // hành Linux, chúng ta không quản ly trực tiếp các block trên
    // đĩa cứng cấp phát cho file, việc quản ly các block của file
    // trên ổ đĩa là một đồ án khác
    if (!fileSystem->Create(filename,0)) {
        printf("\n Error create file '%s'",filename);
        machine->WriteRegister(2,-1);
        delete filename;
        return;
    }
    machine->WriteRegister(2,0); // trả về cho chương trình
    // người dùng thành công
    delete filename;
}

void ExceptionHandlerReadInt() {
    /*int: [-2147483648 , 2147483647] --> max length = 11*/
    const int maxlen = 11;
    char num_string[maxlen] = {0};
    long long ret = 0;
    for (int i = 0; i < maxlen; i++) {
        char c = 0;
        gSynchConsole->Read(&c,1);
        if (c >= '0' && c <= '9') num_string[i] = c;
        else if (i == 0 && c == '-') num_string[i] = c;
        else break;
    }
    int i = (num_string[0] == '-') ? 1 : 0;
    while (i < maxlen && num_string[i] >= '0' && num_string[i] <= '9')
        ret = ret*10 + num_string[i++] - '0';
    ret = (num_string[0] == '-') ? (-ret) : ret;
    machine->WriteRegister(2, (int)ret);
}
void ExceptionHandlerPrintInt() {
    int n = machine->ReadRegister(4);
    /*int: [-2147483648 , 2147483647] --> max length = 11*/
    const int maxlen = 11;
    char num_string[maxlen] = {0};
    int tmp[maxlen] = {0}, i = 0, j = 0;
    if (n < 0) {
        n = -n;
        num_string[i++] = '-';
    }
    do {
        tmp[j++] = n%10;
        n /= 10;
    } while (n);
    while (j) 
        num_string[i++] = '0' + (char)tmp[--j];
    gSynchConsole->Write(num_string,i);
    machine->WriteRegister(2, 0);
}
void ExceptionHandlerReadChar() {
    char c = 0;
    gSynchConsole->Read(&c,1);
    machine->WriteRegister(2, (int)c);
}
void ExceptionHandlerPrintChar() {
    char c = (char)machine->ReadRegister(4);
    gSynchConsole->Write(&c,1);
    machine->WriteRegister(2, 0);
}
void ExceptionHandlerReadString() {
    int buffer = machine->ReadRegister(4);
    int length = machine->ReadRegister(5);
    char *buf = NULL;
    if (length > 0) {
        buf = new char[length];
        if (buf == NULL) {
            char msg[] = "Not enough memory in system.\n";
            gSynchConsole->Write(msg,strlen(msg));
        }
        else
            memset(buf, 0, length);
    }
    if (buf != NULL) {
        /*make sure string is null terminated*/
        gSynchConsole->Read(buf,length-1);
        int n = strlen(buf)+1;
	System2User(buffer, n, buf);
        delete[] buf;
    }	    
    machine->WriteRegister(2, 0);
}
void ExceptionHandlerPrintString() {
    int buffer = machine->ReadRegister(4), i = 0;
    /*limit the length of strings to print both null and non-null terminated strings*/
    const int maxlen = 256;
    char s[maxlen] = {0};
    while (i < maxlen) {
        int oneChar = 0;
        machine->ReadMem(buffer+i, 1, &oneChar);
        if (oneChar == 0) break;
        s[i++] = (char)oneChar;
    }
    gSynchConsole->Write(s,i);
    machine->WriteRegister(2, 0);
}

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------

void ExceptionHandler(ExceptionType which) {
    int type = machine->ReadRegister(2);
	
    // Input: reg4 -filename (string)
    // Output: reg2 -1: error and 0: success
    // Purpose: process the event SC_Create of System call
    // mã system call sẽ được đưa vào thanh ghi r2 (có thể xem lại phần xử lý cho
    // system call Halt trong tập tin start.s ở trên)
    // tham số thứ 1 sẽ được đưa vào thanh ghi r4
    // tham số thứ 2 sẽ được đưa vào thanh ghi r5
    // tham số thứ 3 sẽ được đưa vào thanh ghi r6
    // tham số thứ 4 sẽ được đưa vào thanh ghi r7
    // kết quả thực hiện của system call sẽ được đưa vào thanh ghi r2
    switch (which) {
    case NoException:
        return;
    case SyscallException:
        switch (type){
            case SC_Halt:
                ExceptionHandlerHalt();
                break;
            case SC_Create:
                ExceptionHandlerCreate();
                break;
            case SC_ReadInt:
                ExceptionHandlerReadInt();
                IncreasePC();
                break;
            case SC_PrintInt:
                ExceptionHandlerPrintInt();
                IncreasePC();
                break;
            case SC_ReadChar:
                ExceptionHandlerReadChar();
                IncreasePC();
                break;
            case SC_PrintChar:
                ExceptionHandlerPrintChar();
                IncreasePC();
                break;
            case SC_ReadString:
                ExceptionHandlerReadString();
                IncreasePC();
                break;
            case SC_PrintString:
                ExceptionHandlerPrintString();
                IncreasePC();
                break;
            default:
                IncreasePC();
        }
        break;
     /*other exceptions: print a error message and halt the machine*/
    case PageFaultException:
        printf("Unexpected user mode exception PageFaultException\n");
        interrupt->Halt();
        break;
    case ReadOnlyException:
        printf("Unexpected user mode exception ReadOnlyException\n");
        interrupt->Halt();
        break;
    case BusErrorException:
        printf("Unexpected user mode exception BusErrorException\n");
        interrupt->Halt();
        break;
    case AddressErrorException:
        printf("Unexpected user mode exception AddressErrorException\n");
        interrupt->Halt();
        break;
    case OverflowException:
        printf("Unexpected user mode exception OverflowException\n");
        interrupt->Halt();
        break;
    case IllegalInstrException:
        printf("Unexpected user mode exception IllegalInstrException\n");
        interrupt->Halt();
        break;
    case NumExceptionTypes:
        printf("Unexpected user mode exception NumExceptionTypes\n");
        interrupt->Halt();
        break;
    default:
        printf("\n Unexpected user mode exception (%d %d)", which, type);
        interrupt->Halt();
    }
}
