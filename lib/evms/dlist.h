/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Module: dlist.h
 *
 * Functions: dlist_t     CreateList
 *            int         InsertItem
 *            int         InsertObject
 *            int         DeleteItem
 *            int         DeleteAllItems
 *            int         GetItem
 *            int         GetNextItem
 *            int         GetPreviousItem
 *            int         GetObject
 *            int         BlindGetObject
 *            int         GetNextObject
 *            int         GetPreviousObject
 *            int         ExtractItem
 *            int         ExtractObject
 *            int         BlindExtractObject
 *            int         ReplaceItem
 *            int         ReplaceObject
 *            int         GetTag
 *            int         GetHandle
 *            int         GetListSize
 *            BOOLEAN     ListEmpty
 *            BOOLEAN     AtEndOfList
 *            BOOLEAN     AtStartOfList
 *            int         DestroyList
 *            int         NextItem
 *            int         PreviousItem
 *            int         GoToStartOfList
 *            int         GoToEndOfList
 *            int         GoToSpecifiedItem
 *            int         SortList
 *            int         ForEachItem
 *            int         PruneList
 *            int         AppendList
 *            int         TransferItem
 *            int         CopyList
 *            BOOLEAN     CheckListIntegrity
 *
 * Description:  This module implements a simple, generic, doubly linked list.
 *               Data objects of any type can be placed into a linked list
 *               created by this module.  Furthermore, data objects of different
 *               types may be placed into the same linked list.
 *
 * Notes:  This linked list implementation makes use of the concept of the
 *         current item.  In any non-empty list, one item in the list will
 *         be designated as the current item.  When any of the following
 *         functions are called, they will operate upon the current item
 *         only: GetItem, ReplaceItem, DeleteItem, GetTag, NextItem,
 *         PreviousItem, GetObject, ExtractItem, and ExtractObject.  The
 *         user of this module may set the current item through the use of
 *         the GoToStartOfList, GoToEndOfList, NextItem, PreviousItem,
 *         and GoToSpecifiedItem functions.
 *
 *         Since a linked list created by this module may contain items
 *         of different types, the user will need a way to identify items
 *         of different types which may be in the same list.  To allow users
 *         to do this, the concept of an item tag is used.  When an item is
 *         added to the list, the user must enter an item tag.  The item
 *         tag is merely some identifier that the user wishes to associate
 *         with the item being placed into the list.  When used as intended,
 *         each type of data item will have a unique tag associated with it.
 *         This way, all data items of the same type will have the same tag
 *         while data items of different types will have different tags.
 *         Thus, by using the GetTag function, the user can get the item
 *         tag for the current item without having to get the item from the
 *         list.  This allows the user to differentiate between items of
 *         different types which reside in the same list.
 *
 *         This module is single threaded.  If used in a multi-threaded
 *         environment, the user must implement appropriate access controls.
 *
 *         When an item is inserted or appended to a list, this module
 *         allocates memory on the heap to hold the item and then copies
 *         the item to the memory that it allocated.  This allows local
 *         variables to be safely inserted or appended to a list.  However,
 *         it should be noted that under certain circumstances a copy of the
 *         entire data item will NOT be made.  Specifically, if the data item
 *         is a structure or array containing pointers, then the data pointed
 *         to by the pointers will NOT be copied even though the structure or
 *         array is!  This results from the fact that, when an item is being
 *         inserted or appended to a list, the user provides just an address
 *         and size.  This module assumes that the item to inserted or append
 *         lies in a contiguous block of memory at the address provided by the
 *         user.  This module has no way of knowing the structure of the data
 *         at the specified address, and therefore can not know about any
 *         embedded pointers which may lie within that block of memory.
 *
 *         This module now employs the concept of a handle.  A handle is a
 *         reference to a specific item in a list which allows that item to
 *         be made the current item in the list quickly.  Example:  If you
 *         use the GetHandle function to get a handle for the current item
 *         (lets call the item B1), then, regardless of where you are in the
 *         list (or any reodering of the items in the list), you can make item
 *         B1 the current item by passing its handle to the GoToSpecifiedItem
 *         function.  Alternatively, you could operate directly on B1 using
 *         the other handle based functions, such as GetItem_By_Handle, for
 *         example.  GetItem_By_Handle gets the item associated with the
 *         specified handle without changing which item in the list is the
 *         current item in the list.
 *
 *         The functions of this module refer to user data as either items or
 *         objects.  The difference between the two is simple, yet subtle.  It
 *         deals with who is responsible for the memory used to hold the data.
 *         In the case of an item, this module is responsible for the memory
 *         used to hold the user data.  In the case of an object, the user
 *         is responsible for the memory used to hold the data.
 *
 *         What this means is that, for functions adding ITEMS to a list,
 *         this module will be responsible for allocating memory to hold
 *         the user data and then copying the user data into the memory
 *         that was allocated.  For functions which return items, this
 *         module will COPY the user data from the LIST into a buffer
 *         specified by the user.  For functions which add objects to a
 *         list, the user provides a pointer to a block of memory holding
 *         user data.  This block of memory was allocated by the user, and
 *         becomes the "property" of this module once it has been added to
 *         a LIST.  For functions which return objects, a pointer to the
 *         memory where the data is stored is returned.  As long as an item/object
 *         is in a LIST, this module will be responsible for the memory that
 *         is used to store the data associated with that item.  This means that
 *         users of this module should not call free on an object returned by this
 *         module as long as that object is still within a list.
 *
 *
 */

#ifndef DLISTHANDLER

#define DLISTHANDLER  1

#include <stdlib.h>
#include <errno.h>

#ifndef BOOLEAN_DEFINED
  #define BOOLEAN_DEFINED 1
  typedef unsigned char BOOLEAN;
#endif

typedef void *          ADDRESS;
typedef ulong           TAG;

struct LinkNodeRecord
{
  ADDRESS                   DataLocation;        /* Where the data associated with this LinkNode is */
  uint                      DataSize;            /* The size of the data associated with this LinkNode. */
  TAG                       DataTag;             /* The item tag the user gave to the data. */
  struct MasterListRecord * ControlNodeLocation; /* The control node of the list containing this item. */
  struct LinkNodeRecord *   NextLinkNode;        /* The LinkNode of the next item in the list. */
  struct LinkNodeRecord *   PreviousLinkNode;    /* The LinkNode of the item preceding this one in the list. */
};

typedef struct LinkNodeRecord LinkNode;

struct MasterListRecord
{
  uint            ItemCount;             /* The number of items in the list. */
  LinkNode *      StartOfList;           /* The address of the LinkNode of the first item in the list. */
  LinkNode *      EndOfList;             /* The address of the LinkNode of the last item in the list. */
  LinkNode *      CurrentItem;           /* The address of the LinkNode of the current item in the list. */
#ifdef USE_POOLMAN
  POOL            NodePool;              /* The pool of LinkNodes for this dlist_t. */
#endif
  uint            Verify;                /* A field to contain the VerifyValue which marks this as a list created by this module. */
};

typedef struct MasterListRecord ControlNode;


typedef ControlNode *   dlist_t;


#ifndef TRUE
  #define TRUE 1
#endif
#ifndef FALSE
  #define FALSE 0
#endif


typedef enum _Insertion_Modes {
                                InsertAtStart,
                                InsertBefore,
                                InsertAfter,
                                AppendToList,
                              } Insertion_Modes;

/* Update the IS_DLIST_ERROR() macro below if you add, remove, or change */
/* error codes.                                                          */

#define DLIST_SUCCESS                    0
#define DLIST_OUT_OF_MEMORY              ENOMEM

#define DLIST_CORRUPTED                  201
#define DLIST_BAD                        202
#define DLIST_NOT_INITIALIZED            203
#define DLIST_EMPTY                      204
#define DLIST_ITEM_SIZE_WRONG            205
#define DLIST_BAD_ITEM_POINTER           206
#define DLIST_ITEM_SIZE_ZERO             207
#define DLIST_ITEM_TAG_WRONG             208
#define DLIST_END_OF_LIST                209
#define DLIST_ALREADY_AT_START           210
#define DLIST_BAD_HANDLE                 211
#define DLIST_INVALID_INSERTION_MODE     212
#define DLIST_OBJECT_NOT_FOUND           213
#define DLIST_OBJECT_ALREADY_IN_LIST     214

/* Macro to determine if an error code is a dlist error code. */

#define IS_DLIST_ERROR(rc) ((abs(rc) >= DLIST_CORRUPTED) && (abs(rc) <= DLIST_OBJECT_ALREADY_IN_LIST))

/* The following code is special.  It is for use with the PruneList and ForEachItem functions.  Basically, these functions
   can be thought of as "searching" a list.  They present each item in the list to a user supplied function which can then
   operate on the items.  If the user supplied function returns a non-zero error code, ForEachItem and PruneList abort and
   return an error to the caller.  This may be undesirable.  If the user supplied function used with PruneList and ForEachItem
   returns the code below, PruneList/ForEachItem will abort and return DLIST_SUCCESS.  This allows PruneList and ForEachItem
   to be used to search a list and terminate the search when the desired item is found without having to traverse the
   remaining items in the list.                                                                                                  */

#define DLIST_SEARCH_COMPLETE  0xFF

#ifdef USE_POOLMAN


/*********************************************************************/
/*                                                                   */
/*   Function Name:  CreateList                                      */
/*                                                                   */
/*   Descriptive Name: This function allocates and initializes the   */
/*                     data structures associated with a list and    */
/*                     then returns a pointer to these structures.   */
/*                                                                   */
/*   Input: uint       InitialPoolSize - Each List gets a pool of    */
/*                                     link nodes.  When items are   */
/*                                     added to the List, a link node*/
/*                                     is removed from the pool.     */
/*                                     When an item is removed from  */
/*                                     the List, the link node used  */
/*                                     for that item is returned to  */
/*                                     the pool.  InitialPoolSize is */
/*                                     the number of link nodes to   */
/*                                     place in the pool when the    */
/*                                     pool is created.              */
/*          uint       MaximumPoolSize - When the pool runs out of   */
/*                                     link nodes, new nodes are     */
/*                                     allocated by the pool.  When  */
/*                                     these links start being       */
/*                                     returned to the pool, the pool*/
/*                                     will grow.  This parameter    */
/*                                     puts a limit on how big the   */
/*                                     pool may grow to.  Once the   */
/*                                     pool reaches this size, any   */
/*                                     link nodes being returned to  */
/*                                     the pool will be deallocated. */
/*          uint       PoolIncrement - When the pool runs out of link*/
/*                                   nodes and more are required,    */
/*                                   the pool will allocate one or   */
/*                                   more link nodes.  This tells the*/
/*                                   pool how many link nodes to     */
/*                                   allocate at one time.           */
/*                                                                   */
/*   Output: If Success : The function return value will be non-NULL */
/*                                                                   */
/*           If Failure : The function return value will be NULL.    */
/*                                                                   */
/*   Error Handling:  The function will only fail if it can not      */
/*                    allocate enough memory to create the new list  */
/*                    and its associated pool of link nodes.         */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  None.                                                   */
/*                                                                   */
/*********************************************************************/
dlist_t CreateList(uint InitialPoolSize,
                   uint MaximumPoolSize,
                   uint PoolIncrement);

#else


/*********************************************************************/
/*                                                                   */
/*   Function Name:  CreateList                                      */
/*                                                                   */
/*   Descriptive Name: This function allocates and initializes the   */
/*                     data structures associated with a list and    */
/*                     then returns a pointer to these structures.   */
/*                                                                   */
/*   Input: None.                                                    */
/*                                                                   */
/*   Output: If Success : The function return value will be non-NULL */
/*                                                                   */
/*           If Failure : The function return value will be NULL.    */
/*                                                                   */
/*   Error Handling:  The function will only fail if it can not      */
/*                    allocate enough memory to create the new list. */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  None.                                                   */
/*                                                                   */
/*********************************************************************/
dlist_t       CreateList( void );

#endif


/*********************************************************************/
/*                                                                   */
/*   Function Name: InsertItem                                       */
/*                                                                   */
/*   Descriptive Name:  This function inserts an item into a dlist_t.*/
/*                      The item can be placed either before or      */
/*                      after the current item in the dlist_t.       */
/*                                                                   */
/*   Input:  dlist_t        ListToAddTo : The list to which the      */
/*                                        data item is to be         */
/*                                        added.                     */
/*           uint          ItemSize : The size of the data item, in  */
/*                                    bytes.                         */
/*           ADDRESS       ItemLocation : The address of the data    */
/*                                        to append to the list      */
/*           TAG           ItemTag : The item tag to associate with  */
/*                                   item being appended to the list */
/*           ADDRESS TargetHandle : The item in ListToAddTo which    */
/*                                   is used to determine where      */
/*                                   the item being transferred will */
/*                                   be placed.  If this is NULL,    */
/*                                   then the current item in        */
/*                                   ListToAddTo will be used.       */
/*           Insertion_Modes InsertMode : This indicates where,      */
/*                                   relative to the item in         */
/*                                   ListToAddTo specified by        */
/*                                   Target_Handle, the item being   */
/*                                   inserted can be placed.         */
/*           BOOLEAN MakeCurrent : If TRUE, the item being inserted  */
/*                                 into ListToAddTo becomes the      */
/*                                 current item in ListToAddTo.      */
/*           ADDRESS    * Handle : The address of a variable to hold */
/*                                 the handle for the item that was  */
/*                                 inserted into the list.           */
/*                                                                   */
/*   Output:  If all went well, the return value will be             */
/*            DLIST_SUCCESS and *Handle will contain the ADDRESS of  */
/*            the new item.  If errors were encountered, the   .     */
/*            return value will be the error code and *Handle will   */
/*            be NULL.                                               */
/*                                                                   */
/*   Error Handling: This function will fail under the following     */
/*                   conditions:                                     */
/*                       ListToAddTo does not point to a valid       */
/*                           list                                    */
/*                       ItemSize is 0                               */
/*                       ItemLocation is NULL                        */
/*                       The memory required to hold a copy of the   */
/*                           item can not be allocated.              */
/*                       The memory required to create a LINK NODE   */
/*                           can not be allocated.                   */
/*                       TargetHandle is invalid or is for an item   */
/*                           in another list.                        */
/*                   If this routine fails, an error code is returned*/
/*                   and any memory allocated by this function is    */
/*                   freed.                                          */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes:  The item to add is copied to the heap to                */
/*           avoid possible conflicts with the usage of              */
/*           local variables in functions which process              */
/*           dlist_ts.  However, a pointer to a local variable       */
/*           should not be appended to the dlist_t.                  */
/*                                                                   */
/*           It is assumed that TargetHandle is valid, or is at least*/
/*           the address of an accessible block of storage.  If      */
/*           TargetHandle is invalid, or is not the address of an    */
/*           accessible block of storage, then a trap or exception   */
/*           may occur.                                              */
/*                                                                   */
/*           It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           this assumption is violated, an exception or trap may   */
/*           occur.                                                  */
/*                                                                   */
/*********************************************************************/
int InsertItem (dlist_t           ListToAddTo,
                uint              ItemSize,
                ADDRESS           ItemLocation,
                TAG               ItemTag,
                ADDRESS           TargetHandle,
                Insertion_Modes   Insert_Mode,
                BOOLEAN           MakeCurrent,
                ADDRESS         * Handle);



/*********************************************************************/
/*                                                                   */
/*   Function Name: InsertObject                                     */
/*                                                                   */
/*   Descriptive Name:  This function inserts an object into a       */
/*                      dlist_t.  The object can be inserted before  */
/*                      or after the current item in the list.       */
/*                                                                   */
/*   Input:  dlist_t        ListToAddTo : The list to which the      */
/*                                        data object is to be       */
/*                                        inserted.                  */
/*           uint          ItemSize : The size of the data item, in  */
/*                                    bytes.                         */
/*           ADDRESS       ItemLocation : The address of the data    */
/*                                        to append to the list      */
/*           TAG           ItemTag : The item tag to associate with  */
/*                                   the item being appended to the  */
/*                                   list                            */
/*           ADDRESS TargetHandle : The item in ListToAddTo which    */
/*                                   is used to determine where      */
/*                                   the item being transferred will */
/*                                   be placed.  If this is NULL,    */
/*                                   then the current item in        */
/*                                   ListToAddTo will be used.       */
/*           Insertion_Modes Insert_Mode : This indicates where,     */
/*                                   relative to the item in         */
/*                                   ListToAddTo specified by        */
/*                                   Target_Handle, the item being   */
/*                                   inserted can be placed.         */
/*           BOOLEAN MakeCurrent : If TRUE, the item being inserted  */
/*                                 into ListToAddTo becomes the      */
/*                                 current item in ListToAddTo.      */
/*           ADDRESS    * Handle : The address of a variable to hold */
/*                                 the handle for the item that was  */
/*                                 inserted into the list.           */
/*                                                                   */
/*   Output:  If all went well, the return value will be             */
/*            DLIST_SUCCESS and *Handle will contain the ADDRESS of  */
/*            the new item.  If errors were encountered, the   .     */
/*            return value will be the error code and *Handle will   */
/*            be NULL.                                               */
/*                                                                   */
/*   Error Handling: This function will fail under the following     */
/*                   conditions:                                     */
/*                       ListToAddTo does not point to a valid       */
/*                           list                                    */
/*                       ItemSize is 0                               */
/*                       ItemLocation is NULL                        */
/*                       The memory required for a LINK NODE can not */
/*                           be allocated.                           */
/*                       TargetHandle is invalid or is for an item   */
/*                           in another list.                        */
/*                   If this routine fails, an error code is returned*/
/*                   and any memory allocated by this function is    */
/*                   freed.                                          */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes:  The item to insert is NOT copied to the heap.  Instead, */
/*           the location of the item is stored in the list.  This   */
/*           is the major difference between InsertObject and        */
/*           InsertItem.  InsertItem allocates memory on the heap,   */
/*           copies the item to the memory it allocated, and stores  */
/*           the address of the memory it allocated in the list.     */
/*           InsertObject stores the address provided by the user.   */
/*                                                                   */
/*           It is assumed that TargetHandle is valid, or is at least*/
/*           the address of an accessible block of storage.  If      */
/*           TargetHandle is invalid, or is not the address of an    */
/*           accessible block of storage, then a trap or exception   */
/*           may occur.                                              */
/*                                                                   */
/*           It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           this assumption is violated, an exception or trap may   */
/*           occur.                                                  */
/*                                                                   */
/*********************************************************************/
int InsertObject (dlist_t           ListToAddTo,
                  uint              ItemSize,
                  ADDRESS           ItemLocation,
                  TAG               ItemTag,
                  ADDRESS           TargetHandle,
                  Insertion_Modes   Insert_Mode,
                  BOOLEAN           MakeCurrent,
                  ADDRESS         * Handle);


/*********************************************************************/
/*                                                                   */
/*   Function Name: ExclusiveInsertObject                            */
/*                                                                   */
/*   Descriptive Name:  This function inserts an object into a       */
/*                      dlist_t.  The object can be inserted before  */
/*                      or after the current item in the list. If    */
/*                      object is already in the list, it is not     */
/*                      added again.                                 */
/*                                                                   */
/*   Input:  dlist_t        ListToAddTo : The list to which the      */
/*                                        data object is to be       */
/*                                        inserted.                  */
/*           uint          ItemSize : The size of the data item, in  */
/*                                    bytes.                         */
/*           ADDRESS       ItemLocation : The address of the data    */
/*                                        to append to the list      */
/*           TAG           ItemTag : The item tag to associate with  */
/*                                   the item being appended to the  */
/*                                   list                            */
/*           ADDRESS TargetHandle : The item in ListToAddTo which    */
/*                                   is used to determine where      */
/*                                   the item being transferred will */
/*                                   be placed.  If this is NULL,    */
/*                                   then the current item in        */
/*                                   ListToAddTo will be used.       */
/*           Insertion_Modes Insert_Mode : This indicates where,     */
/*                                   relative to the item in         */
/*                                   ListToAddTo specified by        */
/*                                   Target_Handle, the item being   */
/*                                   inserted can be placed.         */
/*           BOOLEAN MakeCurrent : If TRUE, the item being inserted  */
/*                                 into ListToAddTo becomes the      */
/*                                 current item in ListToAddTo.      */
/*           ADDRESS    * Handle : The address of a variable to hold */
/*                                 the handle for the item that was  */
/*                                 inserted into the list.           */
/*                                                                   */
/*   Output:  If all went well, the return value will be             */
/*            DLIST_SUCCESS and *Handle will contain the ADDRESS of  */
/*            the new item.  If errors were encountered, the   .     */
/*            return value will be the error code and *Handle will   */
/*            be NULL.                                               */
/*                                                                   */
/*   Error Handling: This function will fail under the following     */
/*                   conditions:                                     */
/*                       ListToAddTo does not point to a valid       */
/*                           list                                    */
/*                       ItemSize is 0                               */
/*                       ItemLocation is NULL                        */
/*                       The memory required for a LINK NODE can not */
/*                           be allocated.                           */
/*                       TargetHandle is invalid or is for an item   */
/*                           in another list.                        */
/*                   If this routine fails, an error code is returned*/
/*                   and any memory allocated by this function is    */
/*                   freed.                                          */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes:  The item to insert is NOT copied to the heap.  Instead, */
/*           the location of the item is stored in the list.  This   */
/*           is the major difference between InsertObject and        */
/*           InsertItem.  InsertItem allocates memory on the heap,   */
/*           copies the item to the memory it allocated, and stores  */
/*           the address of the memory it allocated in the list.     */
/*           InsertObject stores the address provided by the user.   */
/*                                                                   */
/*           It is assumed that TargetHandle is valid, or is at least*/
/*           the address of an accessible block of storage.  If      */
/*           TargetHandle is invalid, or is not the address of an    */
/*           accessible block of storage, then a trap or exception   */
/*           may occur.                                              */
/*                                                                   */
/*           It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           this assumption is violated, an exception or trap may   */
/*           occur.                                                  */
/*                                                                   */
/*********************************************************************/
int ExclusiveInsertObject (dlist_t           ListToAddTo,
                           uint              ItemSize,
                           ADDRESS           ItemLocation,
                           TAG               ItemTag,
                           ADDRESS           TargetHandle,
                           Insertion_Modes   Insert_Mode,
                           BOOLEAN           MakeCurrent,
                           ADDRESS         * Handle);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  DeleteItem                                      */
/*                                                                   */
/*   Descriptive Name:  This function removes the specified item from*/
/*                      the list and optionally frees the memory     */
/*                      associated with it.                          */
/*                                                                   */
/*   Input:  dlist_t     ListToDeleteFrom : The list whose current   */
/*                                         item is to be deleted.    */
/*           BOOLEAN    FreeMemory : If TRUE, then the memory        */
/*                                   associated with the current     */
/*                                   item will be freed.  If FALSE   */
/*                                   then the current item will be   */
/*                                   removed from the list but its   */
/*                                   memory will not be freed.       */
/*           ADDRESS Handle : The handle of the item to get.  This   */
/*                            handle must be of an item which resides*/
/*                            in ListToDeleteFrom, or NULL.  If      */
/*                            NULL is used, then the current item    */
/*                            in ListToDeleteFrom will be deleted.   */
/*                                                                   */
/*   Output:  Return DLIST_SUCCESS if successful, else an error code.*/
/*                                                                   */
/*   Error Handling: This function will fail if ListToDeleteFrom is  */
/*                   not a valid list, or if ListToDeleteFrom is     */
/*                   empty, or if Handle is invalid.                 */
/*                   If this routine fails, an error code is         */
/*                   returned.                                       */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  Items in a list can be accessed in two ways:  A copy of */
/*           the item can be obtained using GetItem and its related  */
/*           calls, or a pointer to the item can be obtained using   */
/*           GetObject and its related calls.  If you have a copy of */
/*           the data and wish to remove the item from the list, set */
/*           FreeMemory to TRUE.  This will remove the item from the */
/*           list and deallocate the memory used to hold it.  If you */
/*           have a pointer to the item in the list (from one of the */
/*           GetObject style functions) and wish to remove the item  */
/*           from the list, set FreeMemory to FALSE.  This removes   */
/*           the item from the list without freeing its memory, so   */
/*           that the pointer obtained with the GetObject style      */
/*           functions is still useable.                             */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list, unless the handle specified belongs   */
/*           to the current item in the list, in which case this     */
/*           function behaves the same as DeleteItem.                */
/*                                                                   */
/*********************************************************************/
int DeleteItem (dlist_t ListToDeleteFrom,
                BOOLEAN FreeMemory,
                ADDRESS Handle);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  DeleteAllItems                                  */
/*                                                                   */
/*   Descriptive Name:  This function deletes all of the items in the*/
/*                      specified list and optionally frees the      */
/*                      memory associated with each item deleted.    */
/*                                                                   */
/*   Input:  dlist_t     ListToDeleteFrom : The list whose items     */
/*                                          are to be deleted.       */
/*           BOOLEAN    FreeMemory : If TRUE, then the memory        */
/*                                   associated with each item in the*/
/*                                   list will be freed.  If FALSE   */
/*                                   then the each item will be      */
/*                                   removed from the list but its   */
/*                                   memory will not be freed.       */
/*                                                                   */
/*   Output:  Return DLIST_SUCCESS if successful, else an error code.*/
/*                                                                   */
/*   Error Handling: This function will fail if ListToDeleteFrom is  */
/*                   not a valid list, or if ListToDeleteFrom is     */
/*                   empty.                                          */
/*                   If this routine fails, an error code is         */
/*                   returned.                                       */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  Items in a list can be accessed in two ways:  A copy of */
/*           the item can be obtained using GetItem and its related  */
/*           calls, or a pointer to the item can be obtained using   */
/*           GetObject and its related calls.  If you have a copy of */
/*           the data and wish to remove the item from the list, set */
/*           FreeMemory to TRUE.  This will remove the item from the */
/*           list and deallocate the memory used to hold it.  If you */
/*           have a pointer to the item in the list (from one of the */
/*           GetObject style functions) and wish to remove the item  */
/*           from the list, set FreeMemory to FALSE.  This removes   */
/*           the item from the list without freeing its memory, so   */
/*           that the pointer obtained with the GetObject style      */
/*           functions is still useable.                             */
/*                                                                   */
/*********************************************************************/
int DeleteAllItems (dlist_t ListToDeleteFrom,
                    BOOLEAN FreeMemory);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  DeleteObject                                    */
/*                                                                   */
/*   Descriptive Name:  This function removes the specified object   */
/*                      from the list.                               */
/*                                                                   */
/*   Input:  dlist_t     ListToDeleteFrom : The list whose current   */
/*                                          item is to be deleted.   */
/*           ADDRESS Object : The address of the object to be removed*/
/*                            from the list.                         */
/*                                                                   */
/*   Output:  Return DLIST_SUCCESS if successful, else an error code.*/
/*                                                                   */
/*   Error Handling: This function will fail if ListToDeleteFrom is  */
/*                   not a valid list, or if ListToDeleteFrom is     */
/*                   empty, or if Handle is invalid.                 */
/*                   If this routine fails, an error code is         */
/*                   returned.                                       */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  This function does not alter which item is the current  */
/*           item in the list, unless the handle specified belongs   */
/*           to the current item in the list, in which case this     */
/*           function behaves the same as DeleteItem.                */
/*                                                                   */
/*********************************************************************/
int DeleteObject (dlist_t ListToDeleteFrom,
                  ADDRESS Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetItem                                         */
/*                                                                   */
/*   Descriptive Name:  This function copies the specified item in   */
/*                      the list to a buffer provided by the caller. */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to be copied and returned*/
/*                                       to the caller.              */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                                 the current item is.              */
/*           ADDRESS     ItemLocation : This is the location of the  */
/*                                      buffer into which the current*/
/*                                      item is to be copied.        */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*           ADDRESS Handle : The handle of the item to get.  This   */
/*                            handle must be of an item which resides*/
/*                            in ListToGetItemFrom, or NULL.  If     */
/*                            NULL, then the current item in the list*/
/*                            will be used.                          */
/*           BOOLEAN MakeCurrent : If TRUE, the item to get will     */
/*                                 become the current item in the    */
/*                                 list.                             */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 The buffer at ItemLocation will contain a copy of */
/*                    the current item from ListToGetItemFrom.       */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                                                                   */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemLocation is NULL                      */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                   If any of these conditions occur, an error code */
/*                   will be returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           this assumption is violated, an exception or trap may   */
/*           occur.                                                  */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is considered a valid     */
/*                 handle corresponding to the current item in the   */
/*                 list.                                             */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list.                                       */
/*                                                                   */
/*********************************************************************/
int          GetItem( dlist_t        ListToGetItemFrom,
                      uint           ItemSize,
                      ADDRESS        ItemLocation,
                      TAG            ItemTag,
                      ADDRESS        Handle,
                      BOOLEAN        MakeCurrent);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetNextItem                                     */
/*                                                                   */
/*   Descriptive Name:  This function advances the current item      */
/*                      pointer and then copies the current item in  */
/*                      the list to a buffer provided by the caller. */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to be copied and returned*/
/*                                       to the caller.              */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                                 the current item is.              */
/*           ADDRESS     ItemLocation : This is the location of the  */
/*                                      buffer into which the current*/
/*                                      item is to be copied.        */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 The buffer at ItemLocation will contain a copy of */
/*                    the current item from ListToGetItemFrom.       */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 The current item pointer will NOT be advanced.    */
/*                     The current item in the list will be the same */
/*                     as before the call to this function.          */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemLocation is NULL                      */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         The current item in the list before this  */
/*                             function is called is the last item   */
/*                             item in the list.                     */
/*                   If any of these conditions occur, an error      */
/*                   code will be returned.                          */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           this assumption is violated, an exception or trap may   */
/*           occur.                                                  */
/*                                                                   */
/*********************************************************************/
int GetNextItem(dlist_t ListToGetItemFrom,
                uint    ItemSize,
                ADDRESS ItemLocation,
                TAG     ItemTag);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetPreviousItem                                 */
/*                                                                   */
/*   Descriptive Name:  This function makes the previous item in the */
/*                      list the current item in the list and then   */
/*                      copies that item to a buffer provided by the */
/*                      user.                                        */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to be copied and returned*/
/*                                       to the caller.              */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                                 the current item is.              */
/*           ADDRESS    ItemLocation : This is the location of the   */
/*                                     buffer into which the current */
/*                                     item is to be copied.         */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 The buffer at ItemLocation will contain a copy of */
/*                    the current item from ListToGetItemFrom.       */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 The current item pointer will NOT be advanced.    */
/*                     The current item in the list will be the same */
/*                     as before the call to this function.          */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemLocation is NULL                      */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         The current item in the list before this  */
/*                             function is called is the last item   */
/*                             item in the list.                     */
/*                   If any of these conditions occur, an error      */
/*                   code will be returned.                          */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           this assumption is violated, an exception or trap may   */
/*           occur.                                                  */
/*                                                                   */
/*********************************************************************/
int GetPreviousItem(dlist_t ListToGetItemFrom,
                    uint    ItemSize,
                    ADDRESS ItemLocation,
                    TAG     ItemTag);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetObject                                       */
/*                                                                   */
/*   Descriptive Name:  This function returns the address of the data*/
/*                      associated with the specified item in the    */
/*                      list.                                        */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to have its address      */
/*                                       returned to the caller.     */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                               the current item is.                */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*           ADDRESS Handle : The handle of the item to get.  This   */
/*                            handle must be of an item which resides*/
/*                            in ListToGetItemFrom, or NULL.  If     */
/*                            NULL, then the current item in the list*/
/*           BOOLEAN MakeCurrent : If TRUE, the item to get will     */
/*                                 become the current item in the    */
/*                                 list.                             */
/*           ADDRESS   * Object : The address of a variable to hold  */
/*                                the ADDRESS of data associated     */
/*                                with the current item.             */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 *Object will be the address of the data           */
/*                 associated with the current item in the list.     */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 *Object will be NULL.                             */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                   If any of these conditions occur, an error code */
/*                   will be returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The user should not free the memory associated with     */
/*           the address returned by this function as the object is  */
/*           still in the list.                                      */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is considered a valid     */
/*                 handle designating the current item in the list.  */
/*                                                                   */
/*           It is assumed that Object is a valid address.  If not,  */
/*           an exception or trap may occur.                         */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list.                                       */
/*                                                                   */
/*********************************************************************/
int GetObject(dlist_t   ListToGetItemFrom,
              uint      ItemSize,
              TAG       ItemTag,
              ADDRESS   Handle,
              BOOLEAN   MakeCurrent,
              ADDRESS * Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  BlindGetObject                                  */
/*                                                                   */
/*   Descriptive Name:  This function returns the address of the data*/
/*                      associated with the specified item in the    */
/*                      list.                                        */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current      */
/*                                       item is to have its address */
/*                                       returned to the caller.     */
/*           uint *     ItemSize : The size of the current item      */
/*           TAG *   ItemTag : The tag of the current item           */
/*           ADDRESS Handle : The handle of the item to get.  This   */
/*                            handle must be of an item which resides*/
/*                            in ListToGetItemFrom, or NULL.  If     */
/*                            NULL, then the current item in the list*/
/*           BOOLEAN MakeCurrent : If TRUE, the item to get will     */
/*                                 become the current item in the    */
/*                                 list.                             */
/*           ADDRESS   * Object : The address of a variable to hold  */
/*                                the ADDRESS of data associated     */
/*                                with the current item.             */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 *Object will be the address of the data           */
/*                 associated with the current item in the list.     */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 *Object will be NULL.                             */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                   If any of these conditions occur, an error code */
/*                   will be returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The user should not free the memory associated with     */
/*           the address returned by this function as the object is  */
/*           still in the list.                                      */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is considered a valid     */
/*                 handle designating the current item in the list.  */
/*                                                                   */
/*           It is assumed that Object is a valid address.  If not,  */
/*           an exception or trap may occur.                         */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list.                                       */
/*                                                                   */
/*********************************************************************/
int BlindGetObject(dlist_t ListToGetItemFrom,
                   uint    * ItemSize,
                   TAG     * ItemTag,
                   ADDRESS   Handle,
                   BOOLEAN   MakeCurrent,
                   ADDRESS * Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetNextObject                                   */
/*                                                                   */
/*   Descriptive Name:  This function advances the current item      */
/*                      pointer and then returns the address of the  */
/*                      data associated with the current item in the */
/*                      list.                                        */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to be copied and returned*/
/*                                       to the caller.              */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                               the current item is.                */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*           ADDRESS   * Object : The address of a variable to hold  */
/*                                the ADDRESS of data associated     */
/*                                with the next item.                */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 *Object will be the address of the data           */
/*                 associated with the current item in the list.     */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 *Object will be NULL.                             */
/*                 The current item pointer will NOT be advanced.    */
/*                     The current item in the list will be the same */
/*                     as before the call to this function.          */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         The current item in the list before this  */
/*                             function is called is the last item   */
/*                             item in the list.                     */
/*                   If any of these conditions occur, an error code */
/*                   will be returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The user should not free the memory associated with     */
/*           the address returned by this function as the object is  */
/*           still in the list.                                      */
/*                                                                   */
/*           It is assumed that Object is a valid address.  If not,  */
/*           an exception or trap may occur.                         */
/*                                                                   */
/*********************************************************************/
int GetNextObject(dlist_t   ListToGetItemFrom,
                  uint      ItemSize,
                  TAG       ItemTag,
                  ADDRESS * Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetPreviousObject                               */
/*                                                                   */
/*   Descriptive Name:  This function makes the previous item in the */
/*                      list the current item and then returns the   */
/*                      address of the data associated with the      */
/*                      current item in the list.                    */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to be copied and returned*/
/*                                       to the caller.              */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                                 the current item is.              */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*           ADDRESS   * Object : The address of a variable to hold  */
/*                                the ADDRESS of data associated     */
/*                                with the previous item.            */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 *Object will be the address of the data           */
/*                 associated with the current item in the list.     */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 *Object will be NULL.                             */
/*                 The current item pointer will NOT be advanced.    */
/*                     The current item in the list will be the same */
/*                     as before the call to this function.          */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         The current item in the list before this  */
/*                             function is called is the last item   */
/*                             item in the list.                     */
/*                   If any of these conditions occur, an error code */
/*                   will be returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The user should not free the memory associated with     */
/*           the address returned by this function as the object is  */
/*           still in the list.                                      */
/*                                                                   */
/*           It is assumed that Object is a valid address.  If not,  */
/*           an exception or trap may occur.                         */
/*                                                                   */
/*********************************************************************/
int GetPreviousObject(dlist_t   ListToGetItemFrom,
                      uint      ItemSize,
                      TAG       ItemTag,
                      ADDRESS * Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  ExtractItem                                     */
/*                                                                   */
/*   Descriptive Name:  This function copies the specified item in   */
/*                      the list to a buffer provided by the caller  */
/*                      and removes the item from the list.          */
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to be copied and returned*/
/*                                       to the caller.              */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                                 the current item is.              */
/*           ADDRESS     ItemLocation : This is the location of the  */
/*                                      buffer into which the current*/
/*                                      item is to be copied.        */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*           ADDRESS Handle : The handle of the item to get.  This   */
/*                            handle must be of an item which resides*/
/*                            in ListToGetItemFrom, or NULL.  If     */
/*                            NULL, then the current item in the list*/
/*                            will be used.                          */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 The buffer at ItemLocation will contain a copy of */
/*                    the current item from ListToGetItemFrom.       */
/*                 The item will have been removed from the list and */
/*                    its memory deallocated.                        */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemLocation is NULL                      */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                   If any of these conditions occur, *Error will   */
/*                   contain a non-zero error code.                  */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           these assumptions are violated, an exception or trap    */
/*           may occur.                                              */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is considered a valid     */
/*                 handle which refers to the current item in the    */
/*                 list.                                             */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list, unless the handle specified belongs   */
/*           to the current item in the list, in which case the      */
/*           item following the current item becomes the current     */
/*           item in the list.  If there is no item following the    */
/*           current item in the list, then the item preceding the   */
/*           current item will become the current item in the list.  */
/*                                                                   */
/*********************************************************************/
int ExtractItem(dlist_t ListToGetItemFrom,
                uint    ItemSize,
                ADDRESS ItemLocation,
                TAG     ItemTag,
                ADDRESS Handle);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  ExtractObject                                   */
/*                                                                   */
/*   Descriptive Name:  This function returns the address of the data*/
/*                      associated with the specified item in the    */
/*                      list and then removes that item from the list*/
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current item */
/*                                       is to be copied and returned*/
/*                                       to the caller.              */
/*           uint       ItemSize : What the caller thinks the size of*/
/*                                 the current item is.              */
/*           TAG     ItemTag : What the caller thinks the item tag   */
/*                             of the current item is.               */
/*           ADDRESS Handle : The handle of the item to get.  This   */
/*                            handle must be of an item which resides*/
/*                            in ListToGetItemFrom, or NULL.  If     */
/*                            NULL, then the current item in the     */
/*                            list will be used.                     */
/*           ADDRESS   * Object : The address of a variable to hold  */
/*                                the ADDRESS of data associated     */
/*                                with the current item.             */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 *Object will be the address of the data           */
/*                 associated with the current item in the list.     */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 *Object will be NULL.                             */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                   If any of these conditions occur, an error code */
/*                   will be returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The user is responsible for the memory associated with  */
/*           the address returned by this function since this        */
/*           function removes that object from the list.  This means */
/*           that, when the user is through with the object, they    */
/*           should free it.                                         */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is considered a valid     */
/*                 handle which refers to the current item in the    */
/*                 list.                                             */
/*                                                                   */
/*           It is assumed that Object is a valid address.  If not,  */
/*           an exception or trap may occur.                         */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list, unless the handle specified belongs   */
/*           to the current item in the list, in which case the      */
/*           item following the current item becomes the current     */
/*           item in the list.  If there is no item following the    */
/*           current item in the list, then the item preceding the   */
/*           current item will become the current item in the list.  */
/*                                                                   */
/*********************************************************************/
int ExtractObject(dlist_t   ListToGetItemFrom,
                  uint      ItemSize,
                  TAG       ItemTag,
                  ADDRESS   Handle,
                  ADDRESS * Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  BlindExtractObject                              */
/*                                                                   */
/*   Descriptive Name:  This function returns the address of the data*/
/*                      associated with the specified item in the    */
/*                      list and then removes that item from the list*/
/*                                                                   */
/*   Input:  dlist_t ListToGetItemFrom : The list whose current      */
/*                                       item is to be copied and    */
/*                                       returned to the caller.     */
/*           uint *     ItemSize : The size of the current item      */
/*           TAG *   ItemTag : The tag of the current item           */
/*           ADDRESS Handle : The handle of the item to get.  This   */
/*                            handle must be of an item which resides*/
/*                            in ListToGetItemFrom, or NULL.  If     */
/*                            NULL, then the current item in the     */
/*                            list will be used.                     */
/*           ADDRESS   * Object : The address of a variable to hold  */
/*                                the ADDRESS of data associated     */
/*                                with the current item.             */
/*                                                                   */
/*   Output:  If Successful :                                        */
/*                 Return DLIST_SUCCESS.                             */
/*                 *Object will be the address of the data           */
/*                 associated with the current item in the list.     */
/*            If Failure :                                           */
/*                 Return an error code.                             */
/*                 *Object will be NULL.                             */
/*                                                                   */
/*   Error Handling: This function will fail under any of the        */
/*                   following conditions:                           */
/*                         ListToGetItemFrom is not a valid list     */
/*                         ItemSize does not match the size of the   */
/*                             current item in the list              */
/*                         ItemTag does not match the item tag       */
/*                             of the current item in the list       */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                   If any of these conditions occur, an error code */
/*                   will be returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The user is responsible for the memory associated with  */
/*           the address returned by this function since this        */
/*           function removes that object from the list.  This means */
/*           that, when the user is through with the object, they    */
/*           should free it.                                         */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is considered a valid     */
/*                 handle which refers to the current item in the    */
/*                 list.                                             */
/*                                                                   */
/*           It is assumed that Object is a valid address.  If not,  */
/*           an exception or trap may occur.                         */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list, unless the handle specified belongs   */
/*           to the current item in the list, in which case the      */
/*           item following the current item becomes the current     */
/*           item in the list.  If there is no item following the    */
/*           current item in the list, then the item preceding the   */
/*           current item will become the current item in the list.  */
/*                                                                   */
/*********************************************************************/
int BlindExtractObject(dlist_t ListToGetItemFrom,
                       uint    * ItemSize,
                       TAG     * ItemTag,
                       ADDRESS   Handle,
                       ADDRESS * Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  ReplaceItem                                     */
/*                                                                   */
/*   Descriptive Name:  This function replaces the specified item in */
/*                      the list with the one provided as its        */
/*                      argument.                                    */
/*                                                                   */
/*   Input: dlist_t ListToReplaceItemIn : The list whose current item*/
/*                                       is to be replaced           */
/*          uint    ItemSize : The size, in bytes, of the            */
/*                             replacement item                      */
/*          ADDRESS ItemLocation : The address of the replacement    */
/*                                 item                              */
/*          TAG     ItemTag : The item tag that the user wishes to   */
/*                            associate with the replacement item    */
/*          ADDRESS Handle : The handle of the item to get.  This    */
/*                           handle must be of an item which resides */
/*                           in ListToGetItemFrom, or NULL.  If NULL */
/*                           then the current item in the list will  */
/*                           used.                                   */
/*          BOOLEAN MakeCurrent : If TRUE, the item to get will      */
/*                                become the current item in the     */
/*                                list.                              */
/*                                                                   */
/*   Output:  If Successful then return DLIST_SUCCESS.               */
/*            If Unsuccessful, then return an error code.            */
/*                                                                   */
/*   Error Handling:  This function will fail under the following    */
/*                    conditions:                                    */
/*                         ListToReplaceItemIn is empty              */
/*                         ItemSize is 0                             */
/*                         ItemLocation is NULL                      */
/*                         The memory required can not be allocated. */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                    If any of these conditions occurs, an error    */
/*                    code will be returned.                         */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           these assumptions are violated, an exception or trap    */
/*           may occur.                                              */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is a valid handle which   */
/*                 refers to the current item in the list.           */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list.                                       */
/*                                                                   */
/*********************************************************************/
int ReplaceItem(dlist_t ListToReplaceItemIn,
                uint    ItemSize,
                ADDRESS ItemLocation,
                TAG     ItemTag,
                ADDRESS Handle,
                BOOLEAN MakeCurrent);


/*********************************************************************/
/*                                                                   */
/*   Function Name: ReplaceObject                                    */
/*                                                                   */
/*   Descriptive Name:  This function replaces the specified object  */
/*                      in the list with the one provided as its     */
/*                      argument.                                    */
/*                                                                   */
/*   Input: dlist_t ListToReplaceItemIn : The list whose current     */
/*                                       object is to be replaced    */
/*          uint    ItemSize : The size, in bytes, of the            */
/*                             replacement object                    */
/*          ADDRESS ItemLocation : The address of the replacement    */
/*                                 item                              */
/*          TAG     ItemTag : The item tag that the user wishes to   */
/*                            associate with the replacement item    */
/*          ADDRESS Handle : The handle of the item to get.  This    */
/*                           handle must be of an item which resides */
/*                           in ListToGetItemFrom, or NULL.  If NULL */
/*                           then the current item in the list will  */
/*                           be used.                                */
/*          BOOLEAN MakeCurrent : If TRUE, the item to get will      */
/*                                become the current item in the     */
/*                                list.                              */
/*           ADDRESS   * Object : The address of a variable to hold  */
/*                                the ADDRESS of the object that     */
/*                                was replaced.                      */
/*                                                                   */
/*   Output:  If Successful then return DLIST_SUCCESS and the        */
/*              *Object will contain the address of the object that  */
/*              was replaced.                                        */
/*            If Unsuccessful, then return an error code and         */
/*              *Object will be NULL.                                */
/*                                                                   */
/*   Error Handling:  This function will fail under the following    */
/*                    conditions:                                    */
/*                         ListToReplaceItemIn is empty              */
/*                         ItemSize is 0                             */
/*                         ItemLocation is NULL                      */
/*                         The memory required can not be allocated. */
/*                         Handle is invalid, or is for an item      */
/*                             which is not in ListToGetItemFrom     */
/*                    If any of these conditions occurs, an error    */
/*                    code will be returned.                         */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The user is responsible for the memory associated with  */
/*           the object returned by this function as that object is  */
/*           removed from the list.  This means that, when the user  */
/*           is through with the object returned by this function,   */
/*           they should free it.                                    */
/*                                                                   */
/*           It is assumed that if ItemLocation is not NULL, then    */
/*           it is a valid address that can be dereferenced.  If     */
/*           these assumptions are violated, an exception or trap    */
/*           may occur.                                              */
/*                                                                   */
/*           It is assumed that Handle is valid, or is at least the  */
/*           address of an accessible block of storage.  If Handle   */
/*           is invalid, or is not the address of an accessible block*/
/*           of storage, then a trap or exception may occur.         */
/*           NOTE: For this function, NULL is a valid handle for the */
/*                 current item in the list.                         */
/*                                                                   */
/*           It is assumed that Object is a valid address.  If not,  */
/*           an exception or trap may occur.                         */
/*                                                                   */
/*           This function does not alter which item is the current  */
/*           item in the list.                                       */
/*                                                                   */
/*********************************************************************/
int ReplaceObject(dlist_t   ListToReplaceItemIn,
                  uint    * ItemSize,             /* On input - size of new object.  On return = size of old object. */
                  ADDRESS   ItemLocation,
                  TAG     * ItemTag,              /* On input - TAG of new object.  On return = TAG of old object. */
                  ADDRESS   Handle,
                  BOOLEAN   MakeCurrent,
                  ADDRESS * Object);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetTag                                          */
/*                                                                   */
/*   Descriptive Name:  This function returns the item tag associated*/
/*                      with the current item in the list.           */
/*                                                                   */
/*   Input:  dlist_t ListToGetTagFrom : The list from which the item */
/*                                      tag of the current item is to*/
/*                                      be returned                  */
/*           ADDRESS Handle : The handle of the item whose TAG and   */
/*                            size we are to get.  This handle must  */
/*                            be of an item which resides in         */
/*                            in ListToGetTagFrom, or NULL.  If NULL */
/*                            then the current item in the list will */
/*                            be used.                               */
/*           uint       * ItemSize : The size, in bytes, of the      */
/*                                   current item in the list.       */
/*           TAG        * Tag : The address of a variable to hold    */
/*                              the returned tag.                    */
/*                                                                   */
/*   Output:  If successful, the function returns DLIST_SUCCESS.     */
/*               *ItemSize contains the size of the item.  *Tag      */
/*               contains the tag.                                   */
/*            If unsuccessful, an error code is returned.            */
/*                                                                   */
/*   Error Handling: This function will fail if ListToGetTagFrom is  */
/*                   not a valid list or is an empty list.  In either*/
/*                   of these cases, an error code is returned.      */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
int GetTag(dlist_t   ListToGetTagFrom,
           ADDRESS   Handle,
           uint    * ItemSize,
           TAG     * Tag);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetHandle                                       */
/*                                                                   */
/*   Descriptive Name:  This function returns a handle for the       */
/*                      current item in the list.  This handle is    */
/*                      then associated with that item regardless of */
/*                      its position in the list.  This handle can be*/
/*                      used to make its associated item the current */
/*                      item in the list.                            */
/*                                                                   */
/*   Input:  dlist_t ListToGetHandleFrom : The list from which a     */
/*                                         handle is needed.         */
/*           ADDRESS * Handle   : The address of a variable to hold  */
/*                                the handle                         */
/*                                                                   */
/*   Output:  If successful, the function returns DLIST_SUCCESS and  */
/*               *Handle is set to the handle for the current item   */
/*               in ListToGetHandleFrom.                             */
/*            If unsuccessful, an error code is returned and *Handle */
/*               is set to 0.                                        */
/*                                                                   */
/*   Error Handling: This function will fail if ListToGetHandleFrom  */
/*                   is not a valid list or is an empty list.  In    */
/*                   either of these cases, an error code is         */
/*                   returned.                                       */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  The handle returned is a pointer to the LinkNode of the */
/*           current item in the list.  This allows the item to move */
/*           around in the list without losing its associated handle.*/
/*           However, if the item is deleted from the list, then the */
/*           handle is invalid and its use could result in a trap.   */
/*                                                                   */
/*********************************************************************/
int GetHandle (dlist_t   ListToGetHandleFrom,
               ADDRESS * Handle);



/*********************************************************************/
/*                                                                   */
/*   Function Name:  GetListSize                                     */
/*                                                                   */
/*   Descriptive Name:  This function returns the number of items in */
/*                      a list.                                      */
/*                                                                   */
/*   Input:  dlist_t ListToGetSizeOf : The list whose size we wish to*/
/*                                     know                          */
/*           uint       * Size  : The address of a variable to hold  */
/*                                the size of the list.              */
/*                                                                   */
/*   Output:  If successful, the function returns DLIST_SUCCESS and  */
/*               *Size contains the a count of the number of items   */
/*               in the list.                                        */
/*            If unsuccessful, an error code is returned and *Size   */
/*               is set to 0.                                        */
/*                                                                   */
/*   Error Handling: This function will fail if ListToGetSizeOf is   */
/*                   not a valid list.  If this happens, then an     */
/*                   error code is returned.        .                */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  It is assumed that Size contains a valid address. If    */
/*           this assumption is violated, an exception or trap       */
/*           may occur.                                              */
/*                                                                   */
/*********************************************************************/
int GetListSize(dlist_t ListToGetSizeOf,
                uint * Size);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  ListEmpty                                       */
/*                                                                   */
/*   Descriptive Name:  This function returns TRUE if the            */
/*                      specified list is empty, otherwise it returns*/
/*                      FALSE.                                       */
/*                                                                   */
/*   Input:  dlist_t     ListToCheck : The list to check to see if it*/
/*                                     is empty                      */
/*                                                                   */
/*   Output:  If successful, the function returns TRUE if the        */
/*               number of items in the list is 0, otherwise it      */
/*               returns FALSE.                                      */
/*            If unsuccessful, the function returns TRUE.            */
/*                                                                   */
/*   Error Handling: This function will return TRUE if ListToCheck   */
/*                   is not a valid list.                            */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
BOOLEAN ListEmpty(dlist_t ListToCheck);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  AtEndOfList                                     */
/*                                                                   */
/*   Descriptive Name:  This function returns TRUE if the            */
/*                      current item in the list is the last item    */
/*                      in the list.  Returns FALSE otherwise.       */
/*                                                                   */
/*   Input:  dlist_t     ListToCheck : The list to check.            */
/*                                                                   */
/*   Output:  If successful, the function returns TRUE if the        */
/*               current item in the list is the last item in the    */
/*               list.  If it is not the last item in the list,      */
/*               FALSE is returned.                                  */
/*            If unsuccessful, the function returns FALSE.           */
/*                                                                   */
/*   Error Handling: This function will return FALSE ListToCheck is  */
/*                   not a valid list.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
BOOLEAN AtEndOfList(dlist_t ListToCheck);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  AtStartOfList                                   */
/*                                                                   */
/*   Descriptive Name:  This function returns TRUE if the            */
/*                      current item in the list is the first item   */
/*                      in the list.  Returns FALSE otherwise.       */
/*                                                                   */
/*   Input:  dlist_t     ListToCheck : The list to check.            */
/*                                                                   */
/*   Output:  If successful, the function returns TRUE if the        */
/*               current item in the list is the first item in the   */
/*               list.  If it is not the first item in the list,     */
/*               FALSE is returned.                                  */
/*            If unsuccessful, the function returns FALSE            */
/*                                                                   */
/*   Error Handling: This function will return FALSE if ListToCheck  */
/*                   is not a valid list.                            */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
BOOLEAN AtStartOfList(dlist_t ListToCheck);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  DestroyList                                     */
/*                                                                   */
/*   Descriptive Name:  This function releases the memory associated */
/*                      with the internal data structures of a       */
/*                      dlist_t. Once a dlist_t has been destroyed   */
/*                      by this function, it must be reinitialized   */
/*                      before it can be used again.                 */
/*                                                                   */
/*   Input:  dlist_t     ListToDestroy : The list to be eliminated   */
/*                                       from memory.                */
/*           BOOLEAN FreeItemMemory : If TRUE, all items in the list */
/*                                    will be freed.  If FALSE, all  */
/*                                    items in the list are not      */
/*                                    freed, only the list structures*/
/*                                    associated with them are.      */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS                    */
/*            If unsuccessful, return an error code.                 */
/*                                                                   */
/*   Error Handling: This function will fail if ListToDestroy is not */
/*                   a valid list.  If this happens, then an error   */
/*                   code is returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  If FreeItemMemory is TRUE, then this function will try  */
/*           to delete any items which may be in the list.  However, */
/*           since this function has no way of knowing the internal  */
/*           structure of an item, items which contain embedded      */
/*           pointers will not be entirely freed.  This can lead to  */
/*           memory leaks.  The programmer should ensure that any    */
/*           list passed to this function when the FreeItemMemory    */
/*           parameter is TRUE is empty or does not contain any      */
/*           items with embedded pointers.                           */
/*                                                                   */
/*********************************************************************/
int DestroyList(dlist_t * ListToDestroy,
                BOOLEAN   FreeItemMemory);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  NextItem                                        */
/*                                                                   */
/*   Descriptive Name:  This function makes the next item in the list*/
/*                      the current item in the list (i.e. it        */
/*                      advances the current item pointer).          */
/*                                                                   */
/*   Input:  dlist_t     ListToAdvance : The list whose current item */
/*                                       pointer is to be advanced   */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS.                   */
/*            If unsuccessful, return error code.                    */
/*                                                                   */
/*   Error Handling: This function will fail under the following     */
/*                   conditions:                                     */
/*                        ListToAdvance is not a valid list          */
/*                        ListToAdvance is empty                     */
/*                        The current item is the last item in the   */
/*                           list                                    */
/*                   If any of these conditions occurs, then an      */
/*                   error code is returned.                         */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
int NextItem(dlist_t ListToAdvance);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  PreviousItem                                    */
/*                                                                   */
/*   Descriptive Name:  This function makes the previous item in the */
/*                      list the current item in the list.           */
/*                                                                   */
/*   Input:  dlist_t     ListToChange : The list whose current item  */
/*                                      pointer is to be changed     */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS.                   */
/*            If unsuccessful, return an error code.                 */
/*                                                                   */
/*   Error Handling: This function will fail under the following     */
/*                   conditions:                                     */
/*                        ListToChange is not a valid list           */
/*                        ListToChange is empty                      */
/*                        The current item is the first item in the  */
/*                           list                                    */
/*                   If any of these conditions occurs, then return  */
/*                   an error code.                                  */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
int PreviousItem(dlist_t ListToChange);


/*********************************************************************/
/*                                                                   */
/*   Function Name: GoToStartOfList                                  */
/*                                                                   */
/*   Descriptive Name:  This function makes the first item in the    */
/*                      list the current item in the list.           */
/*                                                                   */
/*   Input:  dlist_t     ListToReset : The list whose current item   */
/*                                     is to be set to the first     */
/*                                     item in the list              */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS.                   */
/*            If unsuccessful, return an error code                  */
/*                                                                   */
/*   Error Handling: This function will fail if ListToAdvance is not */
/*                   a valid list.  If this occurs, then an error    */
/*                   code is returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
int GoToStartOfList(dlist_t ListToReset);


/*********************************************************************/
/*                                                                   */
/*   Function Name: GoToEndOfList                                    */
/*                                                                   */
/*   Descriptive Name:  This function makes the last item in the     */
/*                      list the current item in the list.           */
/*                                                                   */
/*   Input:  dlist_t     ListToSet : The list whose current item     */
/*                                   is to be set to the last item   */
/*                                   in the list                     */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS.                   */
/*            If unsuccessful, return an error code                  */
/*                                                                   */
/*   Error Handling: This function will fail if ListToAdvance is not */
/*                   a valid list.  If this occurs, then an error    */
/*                   code is returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*********************************************************************/
int GoToEndOfList(dlist_t ListToSet);


/*********************************************************************/
/*                                                                   */
/*   Function Name: GoToSpecifiedItem                                */
/*                                                                   */
/*   Descriptive Name:  This function makes the item associated with */
/*                      Handle the current item in the list.         */
/*                                                                   */
/*   Input:  dlist_t ListToReposition:  The list whose current item  */
/*                                      is to be set to the item     */
/*                                      associated with Handle.      */
/*           ADDRESS Handle : A handle obtained by using the         */
/*                            GetHandle function.  This handle       */
/*                            identifies a unique item in the list.  */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS.                   */
/*            If unsuccessful, return an error code                  */
/*                                                                   */
/*   Error Handling: This function will fail if ListToAdvance is not */
/*                   a valid list.  If this occurs, then an error    */
/*                   code is returned.                               */
/*                                                                   */
/*   Side Effects:  None.                                            */
/*                                                                   */
/*   Notes:  It is assumed that Handle is a valid handle and that    */
/*           the item associated with Handle is still in the list.   */
/*           If these conditions are not met, an exception or trap   */
/*           may occur.                                              */
/*                                                                   */
/*********************************************************************/
int GoToSpecifiedItem(dlist_t ListToReposition,
                      ADDRESS Handle);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  SortList                                        */
/*                                                                   */
/*   Descriptive Name:  This function sorts the contents of a list.  */
/*                      The sorting algorithm used is a stable sort  */
/*                      whose performance is not dependent upon the  */
/*                      initial order of the items in the list.      */
/*                                                                   */
/*   Input: dlist_t ListToSort : The dlist_t that is to be sorted.   */
/*                                                                   */
/*          int (*Compare) ( ... )                                   */
/*                                                                   */
/*              This is a pointer to a function that can compare any */
/*              two items in the list.  It should return -1 if       */
/*              Object1 is less than Object2, 0 if Object1 is equal  */
/*              to Object2, and 1 if Object1 is greater than Object2.*/
/*              This function will be called during the sort whenever*/
/*              the sorting algorithm needs to compare two objects.  */
/*                                                                   */
/*              The Compare function takes the following parameters: */
/*                                                                   */
/*              ADDRESS Object1 : The address of the data for the    */
/*                                first object to be compared.       */
/*              TAG Object1Tag : The user assigned TAG value for the */
/*                               first object to be compared.        */
/*              ADDRESS Object2 : The address of the data for the    */
/*                                second object to be compared.      */
/*              TAG Object2Tag : The user assigned TAG value for the */
/*                               second object to be compared.       */
/*              uint * Error : The address of a variable to hold the */
/*                             error return value.                   */
/*                                                                   */
/*              If this function ever sets *Error to a non-zero value*/
/*              the sort will terminate and the error code will be   */
/*              returned to the caller of the SortList function.     */
/*                                                                   */
/*                                                                   */
/*   Output:  If successful, this function will return DLIST_SUCCESS */
/*               and ListToSort will have been sorted.               */
/*            If unsuccessful, an error code will be returned.       */
/*               The order of the items in ListToSort is undefined   */
/*               and may have changed.                               */
/*                                                                   */
/*   Error Handling: This function will terminate if *Compare sets   */
/*                   *Error to a non-zero value, or if ListToSort    */
/*                   is invalid.  If this function does terminate in */
/*                   the middle of a sort, the order of the items in */
/*                   ListToSort may be different than it was before  */
/*                   the function was called.                        */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes:  This function works by breaking the list into sublists  */
/*           and merging the sublists back into one list.  The size  */
/*           of the sublists starts at 1, and with each pass, the    */
/*           of the sublists is doubled.  The sort ends when the size*/
/*           of a sublist is greater than the size of the original   */
/*           list.                                                   */
/*                                                                   */
/*********************************************************************/
int SortList(dlist_t ListToSort,
             int   (*Compare) (ADDRESS   Object1,
                               TAG       Object1Tag,
                               ADDRESS   Object2,
                               TAG       Object2Tag,
                               uint    * Error));


/*********************************************************************/
/*                                                                   */
/*   Function Name:  ForEachItem                                     */
/*                                                                   */
/*   Descriptive Name:  This function passes a pointer to each item  */
/*                      in a list to a user provided function for    */
/*                      processing by the user provided function.    */
/*                                                                   */
/*   Input:  dlist_t ListToProcess : The dlist_t whose items are to  */
/*                                   be processed by the user        */
/*                                   provided function.              */
/*                                                                   */
/*           int (*ProcessItem) (...)                                */
/*                                                                   */
/*               This is a pointer to the user provided function.    */
/*               This user provided function takes the following     */
/*                  parameters:                                      */
/*                                                                   */
/*                  ADDRESS Object : A pointer to an item in         */
/*                                   ListToProcess.                  */
/*                  TAG Object1Tag : The user assigned TAG value for */
/*                                   the item pointed to by Object.  */
/*                  ADDRESS Parameter : The address of a block of    */
/*                                      memory containing any        */
/*                                      parameters that the user     */
/*                                      wishes to have passed to this*/
/*                                      function.                    */
/*                                                                   */
/*           ADDRESS Parameters : This field is passed through to    */
/*                                *ProcessItem.  This function does  */
/*                                not even look at the contents of   */
/*                                this field.  This field is here to */
/*                                provide the user a way to pass     */
/*                                additional data to *ProcessItem    */
/*                                that *ProcessItem may need to      */
/*                                function correctly.                */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS.                   */
/*            If unsuccessful, return an error code.                 */
/*                                                                   */
/*   Error Handling: This function aborts immediately when an error  */
/*                   is detected, and any remaining items in the list*/
/*                   will not be processed.                          */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes: This function allows the user to access all of the items */
/*          in a list and perform an operation on them.  The         */
/*          operation performed must not free any items in the list, */
/*          or perform any list operations on the list being         */
/*          processed.                                               */
/*                                                                   */
/*          As an example of when this would be useful, consider a   */
/*          a list of graphic objects (rectangles, triangles, circles*/
/*          etc.)  which comprise a drawing.  To draw the picture    */
/*          that these graphic objects represent, one could build a  */
/*          loop which gets and draws each item.  Another way to     */
/*          do this would be to build a drawing function which can   */
/*          draw any of the graphic objects, and then use that       */
/*          function as the ProcessItem function in a call to        */
/*          ForEachItem.                                             */
/*                                                                   */
/*          If the ProcessItem function returns an error code        */
/*          other than DLIST_SUCCESS, then ForEachItem will terminate*/
/*          and return an error to whoever called it.  The single    */
/*          exception to this is if ProcessItem returns              */
/*          DLIST_SEARCH_COMPLETE, in which case ForEachItem         */
/*          terminates and returns DLIST_SUCCESS.  This is           */
/*          useful for using ForEachItem to search a list and then   */
/*          terminating the search once the desired item is found.   */
/*                                                                   */
/*          A word about the Parameters parameter.  This parameter   */
/*          is passed through to *ProcessItem and is never looked at */
/*          by this function.  This means that the user can put any  */
/*          value they desire into Parameters as long as it is the   */
/*          same size (in bytes) as Parameters.  The intended use of */
/*          Parameters is to allow the user to pass information to   */
/*          *ProcessItem that *ProcessItem may need.  Either way,    */
/*          how Parameters is used is literally up to the user.      */
/*                                                                   */
/*********************************************************************/
int ForEachItem(dlist_t ListToProcess,
                int     (*ProcessItem) (ADDRESS Object,
                                        TAG     ObjectTag,
                                        uint    ObjectSize,
                                        ADDRESS ObjectHandle,
                                        ADDRESS Parameters),
                ADDRESS Parameters,
                BOOLEAN Forward);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  PruneList                                       */
/*                                                                   */
/*   Descriptive Name:  This function allows the caller to examine   */
/*                      each item in a list and optionally delete    */
/*                      it from the list.                            */
/*                                                                   */
/*   Input:  dlist_t ListToProcess : The dlist_t to be pruned.       */
/*                                                                   */
/*           BOOLEAN (*KillItem) (...)                               */
/*                                                                   */
/*               This is a pointer to a user provided function.      */
/*               This user provided function takes the following     */
/*                  parameters:                                      */
/*                                                                   */
/*                  ADDRESS Object : A pointer to an item in         */
/*                                   ListToProcess.                  */
/*                  TAG Object1Tag : The user assigned TAG value for */
/*                                   the item pointed to by Object.  */
/*                  ADDRESS Parameter : The address of a block of    */
/*                                      memory containing any        */
/*                                      parameters that the user     */
/*                                      wishes to have passed to this*/
/*                                      function.                    */
/*                  BOOLEAN * FreeMemory : The address of a BOOLEAN  */
/*                                         variable which this       */
/*                                         function will set to      */
/*                                         either TRUE or FALSE.     */
/*                                         If the function return    */
/*                                         value is TRUE, then the   */
/*                                         value in *FreeMemory will */
/*                                         be examined.  If it is    */
/*                                         TRUE, then PruneList will */
/*                                         free the memory associated*/
/*                                         with the item being       */
/*                                         deleted.  If *FreeMemory  */
/*                                         is FALSE, then the item   */
/*                                         being removed from the    */
/*                                         dlist_t will not be freed,*/
/*                                         and it is up to the user  */
/*                                         to ensure that this memory*/
/*                                         is handled properly.      */
/*                  uint       * Error : The address of a variable to*/
/*                                       hold the error return value.*/
/*                                                                   */
/*           ADDRESS Parameters : This field is passed through to    */
/*                                *KillItem.  This function does     */
/*                                not even look at the contents of   */
/*                                this field.  This field is here to */
/*                                provide the user a way to pass     */
/*                                additional data to *ProcessItem    */
/*                                that *ProcessItem may need to      */
/*                                function correctly.                */
/*                                                                   */
/*                                                                   */
/*   Output:  If successful, return DLIST_SUCCESS.                   */
/*            If unsuccessful, return an error code.                 */
/*                                                                   */
/*   Error Handling: This function aborts immediately when an error  */
/*                   is detected, and any remaining items in the list*/
/*                   will not be processed.                          */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes: This function allows the user to access all of the items */
/*          in a list, perform an operation on them, and then        */
/*          optionally delete ("remove") them from the dlist_t.  The */
/*          operation performed must not free any items in the list, */
/*          or perform any list operations on the list being         */
/*          processed.                                               */
/*                                                                   */
/*          If the KillItem function sets *Error to something other  */
/*          than DLIST_SUCCESS, then PruneList will terminate and    */
/*          return an error to whoever called it.  The single        */
/*          exception to this is if KillItem sets *Error to          */
/*          DLIST_SEARCH_COMPLETE, in which case KillItem            */
/*          terminates and sets *Error to DLIST_SUCCESS.  This is    */
/*          useful for using KillItem to search a list and then      */
/*          terminating the search once the desired item is found.   */
/*                                                                   */
/*          A word about the Parameters parameter.  This parameter   */
/*          is passed through to *ProcessItem and is never looked at */
/*          by this function.  This means that the user can put any  */
/*          value they desire into Parameters as long as it is the   */
/*          same size (in bytes) as Parameters.  The intended use of */
/*          Parameters is to allow the user to pass information to   */
/*          *ProcessItem that *ProcessItem may need.  Either way,    */
/*          how Parameters is used is literally up to the user.      */
/*                                                                   */
/*********************************************************************/
int PruneList(dlist_t ListToProcess,
              BOOLEAN (*KillItem) (ADDRESS   Object,
                                   TAG       ObjectTag,
                                   uint      ObjectSize,
                                   ADDRESS   ObjectHandle,
                                   ADDRESS   Parameters,
                                   BOOLEAN * FreeMemory,
                                   uint    * Error),
              ADDRESS Parameters);

/*********************************************************************/
/*                                                                   */
/*   Function Name:  AppendList                                      */
/*                                                                   */
/*   Descriptive Name: Removes the items in SourceList and appends   */
/*                     them to TargetList.                           */
/*                                                                   */
/*   Input:  dlist_t TargetList : The dlist_t which is to have the   */
/*                                items from SourceList appended to  */
/*                                it.                                */
/*           dlist_t SourceList : The dlist_t whose items are to be  */
/*                                removed and appended to TargetList.*/
/*                                                                   */
/*   Output: If successful, return DLIST_SUCCESS.                    */
/*              SourceList will be empty, and TargetList will contain*/
/*              all of its original items and all of the items that  */
/*              were in SourceList.                                  */
/*           If unsuccessful, return an error code.  SourceList and  */
/*              TargetList will be unmodified.                       */
/*                                                                   */
/*   Error Handling:  This function will abort immediately upon      */
/*                    detection of an error.  All errors that can be */
/*                    detected are detected before the contents of   */
/*                    SourceList are appended to TargetList, so if an*/
/*                    error is detected and the function aborts,     */
/*                    SourceList and TargetList are unaltered.       */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes: None.                                                    */
/*                                                                   */
/*********************************************************************/
int AppendList(dlist_t TargetList,
               dlist_t SourceList);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  TransferItem                                    */
/*                                                                   */
/*   Descriptive Name: Removes an item in SourceList and places in   */
/*                     TargetList.                                   */
/*                                                                   */
/*   Input:  dlist_t SourceList : The dlist_t containing the item    */
/*                                which is to be transferred.        */
/*           ADDRESS SourceHandle : The handle of the item in        */
/*                                   SourceList which is to be       */
/*                                   transferred to another dlist_t. */
/*                                   If this is NULL, then the       */
/*                                   current item in SourceList will */
/*                                   be used.                        */
/*           dlist_t TargetList : The dlist_t which is to receive the*/
/*                                item being transferred.            */
/*           ADDRESS TargetHandle : The item in TargetList which     */
/*                                   is used to determine where      */
/*                                   the item being transferred will */
/*                                   be placed.  If this is NULL,    */
/*                                   then the current item in        */
/*                                   TargetList will be used.        */
/*           Insertion_Modes TransferMode : This indicates where,    */
/*                                   relative to the item in         */
/*                                   TargetList specified by         */
/*                                   Target_Handle, the item being   */
/*                                   transferred can be placed.      */
/*          BOOLEAN MakeCurrent : If TRUE, the item transferred to   */
/*                                TargetList becomes the current     */
/*                                item in TargetList.                */
/*                                                                   */
/*   Output: If successful, return DLIST_SUCCESS, SourceList will be */
/*              empty, and TargetList will contain all of its        */
/*              original items and all of the items that were in     */
/*              SourceList.                                          */
/*           If unsuccessful, an error code will be returned  and    */
/*              SourceList and TargetList will be unmodified.        */
/*                                                                   */
/*   Error Handling:  This function will abort immediately upon      */
/*                    detection of an error.  All errors that can be */
/*                    detected are detected before the contents of   */
/*                    SourceList are appended to TargetList, so if an*/
/*                    error is detected and the function aborts,     */
/*                    SourceList and TargetList are unaltered.       */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes: None.                                                    */
/*                                                                   */
/*********************************************************************/
int TransferItem(dlist_t         SourceList,
                 ADDRESS         SourceHandle,
                 dlist_t         TargetList,
                 ADDRESS         TargetHandle,
                 Insertion_Modes TransferMode,
                 BOOLEAN         MakeCurrent);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  CopyList                                        */
/*                                                                   */
/*   Descriptive Name: Copies the items in SourceList to the         */
/*                     TargetList.                                   */
/*                                                                   */
/*   Input:  dlist_t TargetList : The dlist_t which is to have the   */
/*                                items from SourceList copied to it.*/
/*           dlist_t SourceList : The dlist_t whose items are to be  */
/*                                copied to TargetList.              */
/*                                                                   */
/*   Output: If successful, return DLIST_SUCCESS.                    */
/*              SourceList will be unchanged and TargetList will     */
/*              contain all of its original items and all of the     */
/*              items that were in SourceList.                       */
/*           If unsuccessful, return an error code.  SourceList and  */
/*              TargetList will be unmodified.                       */
/*                                                                   */
/*   Error Handling:  This function will abort immediately upon      */
/*                    detection of an error.  All errors that can be */
/*                    detected are detected before the contents of   */
/*                    SourceList are appended to TargetList, so if an*/
/*                    error is detected and the function aborts,     */
/*                    SourceList and TargetList are unaltered.       */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes: None.                                                    */
/*                                                                   */
/*********************************************************************/
int CopyList(dlist_t         TargetList,
             dlist_t         SourceList,
             Insertion_Modes Insert_Mode);


/*********************************************************************/
/*                                                                   */
/*   Function Name:  CheckListIntegrity                              */
/*                                                                   */
/*   Descriptive Name: Checks the integrity of a dlist_t.  All link  */
/*                     nodes in the list are checked, as are all     */
/*                     fields in the list control block.             */
/*                                                                   */
/*   Input:  dlist_t ListToCheck - The list whose integrity is to be */
/*                                 checked.                          */
/*                                                                   */
/*   Output: The function return value will be TRUE if all of the    */
/*           elements in the dlist_t are correct.  If this function  */
/*           returns FALSE, then the dlist_t being checked has been  */
/*           corrupted!                                              */
/*                                                                   */
/*   Error Handling: If this function encounters an error in a       */
/*                   dlist_t, it will return FALSE.                  */
/*                                                                   */
/*   Side Effects: None.                                             */
/*                                                                   */
/*   Notes: None.                                                    */
/*                                                                   */
/*********************************************************************/
BOOLEAN CheckListIntegrity(dlist_t ListToCheck);


#endif


