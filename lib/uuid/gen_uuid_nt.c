

//
// Use NT api to generate uuid
//



#include "uuidP.h"


#pragma comment(lib, "ntdll.lib")

unsigned long
__stdcall
NtAllocateUuids(
   void* p1,  // 8 bytes
   void* p2,  // 4 bytes
   void* p3   // 4 bytes
   );

void uuid_generate(uuid_t out)
{
	NtAllocateUuids(out, ((char*)out)+8, ((char*)out)+12 );
}
