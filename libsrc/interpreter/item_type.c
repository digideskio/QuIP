#include "quip_config.h"

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>		/* isatty() */
#endif

#ifdef THREAD_SAFE_QUERY
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */
#endif /* THREAD_SAFE_QUERY */

/* This was not defined on old SunOS */
#ifndef tolower
#define tolower(c)		(( c )+'a'-'A')
#endif


#include "quip_prot.h"
#include "query_prot.h"
#include "item_prot.h"

#ifdef QUIP_DEBUG
static u_long item_debug=ITEM_DEBUG_MASK;
static u_long debug_contexts=CTX_DEBUG_MASK;
#endif /* QUIP_DEBUG */

#ifdef HAVE_HISTORY
#include "history.h"	/* add_phist() */
#endif /* HAVE_HISTORY */

/* local prototypes */
//#ifdef CAUTIOUS
//static void check_item_type(Item_Type *itp);
//#endif /* CAUTIOUS */
static void make_needy(Item_Type *itp);
static void init_itp(Item_Type *itp);
static int item_cmp(const void *,const void *);
static Item_Type * init_item_type(QSP_ARG_DECL  const char *name);
static void store_item(QSP_ARG_DECL  Item_Type *itp,Item *ip,Node *np);

static ITEM_INIT_PROT(Item_Context,ctx)
//static ITEM_GET_PROT(Item_Context,ctx)
static ITEM_NEW_PROT(Item_Context,ctx)
static ITEM_DEL_PROT(Item_Context,ctx)

static Item_Type *	new_ittyp(QSP_ARG_DECL  const char *);

static void		dump_item_context(QSP_ARG_DECL  Item_Context *);


#define ITEM_TYPE_STRING	"Item_Type"
Item_Type * ittyp_itp=NO_ITEM_TYPE;

/*static*/ ITEM_INIT_FUNC(Item_Type,ittyp)
static ITEM_NEW_FUNC(Item_Type,ittyp)

ITEM_LIST_FUNC(Item_Type,ittyp)
// move to questions.c
//ITEM_PICK_FUNC(Item_Type,ittyp)

static Item_Type *ctx_itp=NO_ITEM_TYPE;

#define CTX_IT_NAME	"Context"
#define DEF_CTX_NAME	"default"

static ITEM_INIT_FUNC(Item_Context,ctx)
ITEM_CHECK_FUNC(Item_Context,ctx)
//static ITEM_GET_FUNC(Item_Context,ctx)
//static ITEM_LIST_FUNC(Item_Context,ctx)
static ITEM_NEW_FUNC(Item_Context,ctx)
static ITEM_DEL_FUNC(Item_Context,ctx)
/* static ITEM_PICK_FUNC(Item_Context,ctx) */

#define CHECK_ITEM_INDEX( itp )	if( ( itp ) == NO_ITEM_TYPE ){		\
					WARN("Null item type");		\
					return;				\
				}


/* a global */
u_long total_from_malloc = 0;


//#ifdef CAUTIOUS
//static void check_item_type(Item_Type *itp)
//{
//	if( itp == NO_ITEM_TYPE )
//		NERROR1("CAUTIOUS:  Null item type");
//}
//#endif /* CAUTIOUS */

/* If we don't use an ansii style declaration,
 * we get warnings on the pc (microsoft compiler)
 */

static void no_del_method(QSP_ARG_DECL  Item *ip)
{
	WARN("Undefined item deletion method");
	sprintf(ERROR_STRING,"Can't delete item '%s'",ITEM_NAME(ip));
	advise(ERROR_STRING);
}

void set_del_method(QSP_ARG_DECL  Item_Type *itp,void (*func)(QSP_ARG_DECL  Item *))
{
//#ifdef CAUTIOUS
//	if( IT_DEL_METHOD(itp) != no_del_method ){
//		sprintf(ERROR_STRING,
//	"Item type %s already has a deletion method defined!?",IT_NAME(itp));
//		WARN(ERROR_STRING);
//	}
//#endif /* CAUTIOUS */

	assert( IT_DEL_METHOD(itp) == no_del_method );

	SET_IT_DEL_METHOD(itp, func);
}

static void init_itp(Item_Type *itp)
{
	/* should we really do this? */
	SET_IT_LIST(itp, new_list() );
	SET_IT_FREE_LIST(itp, new_list() );
	SET_IT_FLAGS(itp, NEED_CHOICES);
	SET_IT_CHOICES(itp, NO_STR_ARRAY);

	//SET_IT_CTX_IT(itp,NO_ITEM_TYPE);

	SET_IT_CLASS_LIST(itp, NO_LIST);	// this was commented out - why?
	SET_IT_DEL_METHOD(itp, no_del_method);

#ifdef THREAD_SAFE_QUERY
	{
		int i;
//if( n_active_threads==0 )NERROR1("init_itp:  no active threads!?");
		/*
		SET_IT_CSTK_AT_IDX(itp,0,new_stack());
		for(i=1;i<n_active_threads;i++)
			SET_IT_CSTK_AT_IDX(itp,i,new_stack());
		for(;i<MAX_QUERY_STACKS;i++)
			SET_IT_CSTK_AT_IDX(itp,i,NO_LIST);
			*/
		/* What the heck - just allocate all the lists */
		for(i=0;i<MAX_QUERY_STACKS;i++){
			SET_IT_CSTK_AT_IDX(itp,i,new_stack());
			SET_IT_CTX_RESTRICTED_AT_IDX(itp,i,0);
		}
	}
#ifdef HAVE_PTHREADS
	{
		//itp->it_mutex = PTHREAD_MUTEX_INITIALIZER;
		int status;

		status = pthread_mutex_init(&itp->it_mutex,NULL);
		if( status != 0 )
			NERROR1("error initializing mutex");
	}
#endif /* HAVE_PTHREADS */

#else /* ! THREAD_SAFE_QUERY */
	SET_IT_CSTK(itp,new_stack());
	SET_IT_CTX_RESTRICTED(itp,0);
#endif /* ! THREAD_SAFE_QUERY */
}

#ifdef THREAD_SAFE_QUERY

#ifdef NOT_USED

/* In the multi-thread environment, each item type has to have a separate context stack
 * for each query stack.  When we create a new query stack, we have to initialize
 * the corresponding context stack.
 * We call this when we have added a new query stream; But how do we know???
 */

static void setup_item_type_context(QSP_ARG_DECL  Item_Type *itp, Query_Stack *new_qsp)
{
	Item_Context *icp;

	icp = (Item_Context *) BOTTOM_OF_STACK(IT_CSTK_AT_IDX(itp,0));
	push_item_context(new_qsp,itp,icp);
}

// Why not???  BUG?
void setup_all_item_type_contexts(QSP_ARG_DECL  void *new_qsp)
{
	/* Push the default context onto ALL item types */
	List *lp;
	Item_Type *itp;
	Node *np;

	lp = item_list(QSP_ARG  ittyp_itp);
	np=QLIST_HEAD(lp);
	while( np != NO_NODE ){
		itp = (Item_Type *)NODE_DATA(np);
		setup_item_type_context(QSP_ARG  itp,(Query_Stack *)new_qsp);
		np=NODE_NEXT(np);
	}
}
#endif /* NOT_USED */


#endif /* THREAD_SAFE_QUERY */

static Item_Type * init_item_type(QSP_ARG_DECL  const char *name)
{
	static int is_first=1;	// how many times is this auto-initialized?
	Item_Type *itp;
	Item_Context *icp;

//sprintf(ERROR_STRING,"init_item_type %s, is_first = %d",name,is_first);
//NADVISE(ERROR_STRING);
	if( is_first ){		// is this the first item type?  (the item type items...)
		static Item_Context first_item_context;
		/* Item_Context * */ icp=(&first_item_context);
		//Node *np;	// unused?
#ifdef THREAD_SAFE_QUERY
		/* a real hack... */
//		Query_Stack dummy_qs;
#endif /* THREAD_SAFE_QUERY */

		// The first call is for Query_Stack.
		// Item_Type is initialized below to prevent recursion. 
		static Item_Type first_item_type;

//sprintf(ERROR_STRING,"init_item_type:  first_item_type is %s",name);
//advise(ERROR_STRING);

		/* Item_Type's are themselves items...
		 * but we can't call new_item_type to create
		 * the item_type for item_types!
		 */

		/* we don't call new_ittyp() to avoid a recursion problem */
		ittyp_itp = &first_item_type;
		SET_IT_NAME(ittyp_itp, ITEM_TYPE_STRING );
		is_first=0;
		init_itp(ittyp_itp);

		/* We need to create the first context, but we don't want
		 * infinite recursion...
		 */

		/* BUG make sure DEF_CTX_NAME matches what is here... */
		SET_CTX_NAME( icp, savestr("Item_Type.default") );
		SET_CTX_DICT( icp,  create_dictionary("Item_Type.default") );
		SET_CTX_IT( icp, ittyp_itp );
		/*np =*/ mk_node(icp);

		PUSH_ITEM_CONTEXT(ittyp_itp, icp);

		//addHead(CTX_LIST(FIRST_CONTEXT(ittyp_itp)),np);

#ifdef THREAD_SAFE_QUERY
		/* a real hack... */
		/* What is the purpose of this??? */
//		qsp=&dummy_qs;
//		SET_QS_SERIAL(qsp,0);
#endif /* THREAD_SAFE_QUERY */

		/* why do this? Do we really need to? */
		add_item(QSP_ARG  ittyp_itp,ittyp_itp,NO_NODE);
	}
//#ifdef CAUTIOUS
//	if( !strcmp(name,ITEM_TYPE_STRING) ){
//		WARN("CAUTIOUS:  don't call init_item_type for item_type");
//		return(NO_ITEM_TYPE);
//	}
//#endif /* CAUTIOUS */

	assert( strcmp(name,ITEM_TYPE_STRING) );

	itp = new_ittyp(QSP_ARG  name);
	init_itp(itp);
	icp = create_item_context(QSP_ARG  itp,DEF_CTX_NAME);
	PUSH_ITEM_CONTEXT(itp,icp);

	return(itp);
}

/*
 * Create a new item type.  Allocate a hash table with hashsize
 * entries.  Return the item type index or -1 on failure.
 */

Item_Type * new_item_type(QSP_ARG_DECL  const char *atypename)
{
	Item_Type * itp;

	if( ittyp_itp != NO_ITEM_TYPE ){
		Item *ip;
		ip = item_of(QSP_ARG  ittyp_itp,atypename);
		if( ip != NO_ITEM ){
			sprintf(ERROR_STRING,
			"Item type name \"%s\" is already in use\n",atypename);
			WARN(ERROR_STRING);
			return(NO_ITEM_TYPE);
		}
	}
	/* else we are initializing the item type Item_Type */

	itp=init_item_type(QSP_ARG  atypename);
//#ifdef CAUTIOUS
//	if( itp == NO_ITEM_TYPE )
//		WARN("CAUTIOUS:  new_item_type failed!?");
//#endif /* CAUTIOUS */
	assert( itp != NO_ITEM_TYPE );

	if( ittyp_itp==NO_ITEM_TYPE ){
		ittyp_itp = itp;
	}

	return(itp);
}

/*
 * Put an item into the corresponding name space
 */

static void store_item( QSP_ARG_DECL  Item_Type *itp, Item *ip, Node *np )
{
	if( insert_name(ip,np,CTX_DICT(CURRENT_CONTEXT(itp))) < 0 ){

		/* We used to enlarge the hash table here, but now we automatically enlarge
		 * the hash table before it becomes completely full...
		 */

		sprintf(ERROR_STRING,
			"Error storing name %s",ITEM_NAME(ip));
		NERROR1(ERROR_STRING);
	}
}

/* This routine was eliminated, but we have reinstated it, so that
 * command tables can be preallocated items.
 */

int add_item( QSP_ARG_DECL  Item_Type *itp, void *ip, Node *np )
{
//#ifdef CAUTIOUS
//	check_item_type( itp );
//#endif /* CAUTIOUS */
	assert( itp != NULL );

	store_item(QSP_ARG  itp,(Item*) ip,np);

	/*
	if( np==NO_NODE )
	*/
	/*
		SET_IT_FLAG_BITS(itp, CONTEXT_CHANGED);

	SET_IT_FLAG_BITS(itp, NEED_CHOICES);
	*/
	make_needy(itp);

	return 0;
}

/*
 * Return a ptr to a new item.  Reuse a previously freed item
 * if one exists, otherwise allocate some new memory.
 * NOTE:  this memory is not obtained with getbuf, so it can't be
 * freed with givbuf!!
 *
 * OK, I'll bite - why is it allocated with malloc instead of getbuf?
 * Remember that getbuf was originally introduced because of distrust of
 * malloc on a very old system, but now it is still useful occasionally
 * for tracking down memory leaks...
 */

Item *new_item( QSP_ARG_DECL  Item_Type *itp, const char* name, size_t size )
{
	Item *ip;
	Node *np;

//#ifdef CAUTIOUS
//	if( *name == 0 ){
//		sprintf(ERROR_STRING,
//	"CAUTIOUS Can't create item of type \"%s\" with null name",
//			IT_NAME(itp));
//		WARN(ERROR_STRING);
//		return(NO_ITEM);
//	}
//#endif /* CAUTIOUS */
	assert( *name != 0 );

	LOCK_ITEM_TYPE(itp)

	/* We will allow name conflicts if they are not in the same context */

	/* Only check for conflicts in the current context */
	ip = fetch_name(name,CTX_DICT(CURRENT_CONTEXT(itp)));

	if( ip != NO_ITEM ){
		UNLOCK_ITEM_TYPE(itp);
		sprintf(ERROR_STRING,
	"%s name \"%s\" is already in use in context %s",
			IT_NAME(itp),name,CTX_NAME(CURRENT_CONTEXT(itp)));
		WARN(ERROR_STRING);
		return(NO_ITEM);
	}

	// Try to get a structure from the free list
	// If the free list is empty, then allocate a page's worth

	if( QLIST_HEAD(IT_FREE_LIST(itp)) == NO_NODE ){
		int n_per_page;
		char *nip;

#define FOUR_K	4096

		n_per_page = FOUR_K / size;	/* BUG use PAGESIZE */

		if( n_per_page <= 0 ){
			/* cast size to u_long because size_t is u_long on IA64 and
			 * u_int on IA32!?
			 */
			// If the item is bigger than a page, just get one
			n_per_page=1;
		}

#ifdef QUIP_DEBUG
if( debug & item_debug ){
sprintf(ERROR_STRING,"malloc'ing %d more %s items",n_per_page,IT_NAME(itp));
NADVISE(ERROR_STRING);
}
#endif /* QUIP_DEBUG */
		/* get a pages worth of items */
		nip = (char*)  malloc( n_per_page * size );
		total_from_malloc += n_per_page*size;

		if( nip == NULL ){
			sprintf(ERROR_STRING,
		"new_item:  out of memory while getting a new page of %s's",
				IT_NAME(itp));
			NERROR1(ERROR_STRING);
		}

		while(n_per_page--){
			np = mk_node(nip);
			addTail(IT_FREE_LIST(itp),np);
			nip += size;
		}
	}

	np = remHead(IT_FREE_LIST(itp));

//#ifdef CAUTIOUS
//	if( np == NO_NODE ){
//		NERROR1("CAUTIOUS:  new_item:  couldn't remove node from item free list!?");
//		IOS_RETURN_VAL(NULL)
//	}
//#endif /* CAUTIOUS */
	assert( np != NO_NODE );

	ip = (Item *) NODE_DATA(np);
	SET_ITEM_NAME( ip, savestr(name) );
//sprintf(ERROR_STRING,"Item name %s stored at 0x%lx",ITEM_NAME(ip),(u_long)ITEM_NAME(ip));
//advise(ERROR_STRING);

#ifdef ITEMS_KNOW_OWN_TYPE
	SET_ITEM_TYPE( ip, itp );
#endif /* ITEMS_KNOW_OWN_TYPE */

#ifdef BUILD_FOR_OBJC
	SET_ITEM_MAGIC(ip,QUIP_ITEM_MAGIC);
#endif /* BUILD_FOR_OBJC */

	/* BUG? should we worry about nodes here? */

	add_item(QSP_ARG  itp,ip,np);

	UNLOCK_ITEM_TYPE(itp)

	return(ip);
} // end new_item

#ifdef NOT_USED
void list_item_contexts( QSP_ARG_DECL   Item_Type *itp )
{
	Node *np;

	if( CONTEXT_LIST(itp)==NO_LIST ||
		(np=QLIST_HEAD(CONTEXT_LIST(itp)))==NO_NODE){

		sprintf(ERROR_STRING,"Item type \"%s\" has no contexts",
			IT_NAME(itp));
		NADVISE(ERROR_STRING);
		return;
	}

	while(np!=NO_NODE ){
		Item_Context *icp;

		icp=(Item_Context *) NODE_DATA(np);
		sprintf(MSG_STR,"%s",CTX_NAME(icp));
		prt_msg(MSG_STR);

		np=NODE_NEXT(np);
	}
}
#endif /* NOT_USED */

/* Create a new context with the given name.
 * It needs to be push'ed in order to make it be
 * the current context for new item creation.
 */

Item_Context * create_item_context( QSP_ARG_DECL  Item_Type *itp, const char* name )
{
	Item_Context *icp;
	char cname[LLEN];

	/* maybe we should have contexts for contexts!? */

	sprintf(cname,"%s.%s",IT_NAME(itp),name);

	if( (!strcmp(IT_NAME(itp),CTX_IT_NAME)) && !strcmp(name,DEF_CTX_NAME) ){
		static Item_Context first_context;

		/* can't use new_ctx()
		 *
		 * Why not???
		 */
		icp = &first_context;
		SET_CTX_NAME( icp, savestr(cname) );
		SET_CTX_IT( icp, itp );
		SET_CTX_DICT(icp , create_dictionary(CTX_NAME(icp)) );
		SET_CTX_FLAGS(icp,0);
		/* BUG?  not in the context database?? */
		return(icp);
	}

	/* Create an item type for contexts.
	 *
	 * Because new_item_type() calls create_item_text for the default
	 * context, we have the special case above...
	 */

	if( ctx_itp == NO_ITEM_TYPE )
		ctx_itp = new_item_type(QSP_ARG  CTX_IT_NAME);

#ifdef QUIP_DEBUG
if( debug & qldebug ){
sprintf(ERROR_STRING,"%s - %s:  creating context %s",
WHENCE(create_item_context),cname);
advise(ERROR_STRING);
}
#endif	/* QUIP_DEBUG */

	icp = new_ctx(QSP_ARG  cname);

	/* If it_context_itp was null, make sure it has the right value now */
	// This is the item type for ALL the contexts of all item types, not just
	// for this item type...  The context names typically have the item name
	// and a single dot as a prefix...
//	SET_IT_CTX_IT(itp,ctx_itp);

	if( icp == NO_ITEM_CONTEXT ){
		return(icp);
	}

	SET_CTX_IT( icp, itp );
	SET_CTX_DICT(icp , create_dictionary(CTX_NAME(icp)) );
	SET_CTX_FLAGS(icp,0);

//#ifdef CAUTIOUS
//	if( CTX_DICT(icp) == NO_DICTIONARY ){
//		sprintf(ERROR_STRING,
//	"CAUTIOUS:  error creating dictionary %s",CTX_NAME(icp));
//		NERROR1(ERROR_STRING);
//	}
//#endif /* CAUTIOUS */
	assert( CTX_DICT(icp) != NO_DICTIONARY );

	return(icp);
}


#ifdef FOO
void show_context_stack(Item_Type *itp)
{
	Item_Context *icp;
	Node *np;

	if( CONTEXT_LIST(itp)==NO_LIST ){
none:
		sprintf(ERROR_STRING,"No contexts in existence for %s items",IT_NAME(itp));
		NADVISE(ERROR_STRING);
		return;
	}
	np=QLIST_HEAD(CONTEXT_LIST(itp));
	if( np == NO_NODE ) goto none;

	sprintf(ERROR_STRING,"%s contexts:",IT_NAME(itp));
	NADVISE(ERROR_STRING);
	while( np != NO_NODE ){
		icp = NODE_DATA(np);
		sprintf(ERROR_STRING,"\t%s",CTX_NAME(icp));
		NADVISE(ERROR_STRING);
		np=NODE_NEXT(np);
	}
}
#endif /* FOO */

/* push an existing context onto the top of stack for this item class */

void push_item_context( QSP_ARG_DECL   Item_Type *itp, Item_Context *icp )
{
	Node *np;

	/* might be a good idea to check here that the context item type
	 * matches itp.
	 */

//#ifdef CAUTIOUS
//	if( icp == NO_ITEM_CONTEXT ){
//		sprintf(ERROR_STRING,"CAUTIOUS:  attempted to push null context, %s item type!?",
//			IT_NAME(itp));
//		ERROR1(ERROR_STRING);
//		return; // iOS - error1 can return
//	}
//#endif
	assert( icp != NO_ITEM_CONTEXT );

#ifdef QUIP_DEBUG
if( debug & debug_contexts ){
sprintf(ERROR_STRING,"push_item_context:  pushing %s context %s",IT_NAME(itp),CTX_NAME(icp));
NADVISE(ERROR_STRING);
}
#endif /* QUIP_DEBUG */

	// This is really a stack push!
	np=mk_node(icp);
	if( CONTEXT_LIST(itp) == NO_LIST )
		CONTEXT_LIST(itp) = new_list();
	addHead(CONTEXT_LIST(itp),np);

	SET_CURRENT_CONTEXT(itp, icp);

	// BUG - flags should be qsp-specific???
	SET_IT_FLAG_BITS(itp, NEED_CHOICES|NEED_LIST );
} // end push_item_context

/*
 * Remove the top-of-stack context, but do not destroy.
 */

Item_Context * pop_item_context( QSP_ARG_DECL  Item_Type *itp )
{
	Node *np;
	Item_Context *new_icp, *popped_icp;

	/* don't remove the context from the list yet, it needs
	 * to be there to find the objects in the context...
	 */
	np=remHead(CONTEXT_LIST(itp));
	if( np==NO_NODE ){
		sprintf(ERROR_STRING,
			"Item type %s has no context to pop",IT_NAME(itp));
		WARN(ERROR_STRING);
		return(NO_ITEM_CONTEXT);
	}
	rls_node(np);

	np=QLIST_HEAD(CONTEXT_LIST(itp));
	new_icp = (Item_Context *) NODE_DATA(np);

	popped_icp = CURRENT_CONTEXT(itp);
	SET_CURRENT_CONTEXT(itp,new_icp);

#ifdef QUIP_DEBUG
if( debug & debug_contexts ){
sprintf(ERROR_STRING,"pop_item_context:  %s context %s popped",IT_NAME(itp),CTX_NAME(popped_icp));
NADVISE(ERROR_STRING);
}
#endif /* QUIP_DEBUG */

	// BUG - flags should be qsp-specific???
	SET_IT_FLAG_BITS(itp, NEED_CHOICES|NEED_LIST );

	return(popped_icp);
}

void delete_item_context( QSP_ARG_DECL  Item_Context *icp )
{
	delete_item_context_with_callback( QSP_ARG  icp, NULL );
}

void delete_item_context_with_callback( QSP_ARG_DECL  Item_Context *icp, void (*func)(Item *) )
{
	Item_Type *itp;
	Node *np;

	/* delete all the items in this context.
	 * Because we use the user-supplied deletion method for the items
	 * in the context, the context must be active (preferably top
	 * of stack) when this is called.
	 * We search the entire context list for references, remove them,
	 * and then push the context back onto the top...
	 *
	 * This requires a user-supplied deletion method to avoid memory leaks.
	 *
	 * The above comment suggests that this routine is responsible for
	 * deleting ALL aspects of the object, not just the dictionary entries?
	 */

	itp = (Item_Type *) CTX_IT(icp);

	while( (np=remData(CONTEXT_LIST(itp),icp)) != NO_NODE ){
		rls_node(np);
	}

	/* make the context be on the top of the stack */
	PUSH_ITEM_CONTEXT(itp,icp);

	if( IT_DEL_METHOD(itp) == no_del_method ){
		sprintf(ERROR_STRING,
	"No object deletion method provided for item type %s",IT_NAME(itp));
		WARN(ERROR_STRING);
	} else {
		List *lp;

		lp=dictionary_list(CTX_DICT(icp));

		/* Don't use remHead to get the node, del_item()
		 * will remove it for us, and put it on the free list.
		 */
		while( lp!=NO_LIST && (np=QLIST_HEAD(lp))!=NO_NODE ){
			Item *ip;
			ip = (Item*) NODE_DATA(np);
			if( func != NULL ) (*func)(ip);
			(*IT_DEL_METHOD(itp))(QSP_ARG  ip);
			/* force list update in case hashing */
			lp=dictionary_list(CTX_DICT(icp));
		}
	}

	delete_dictionary(CTX_DICT(icp));
	/* zap_hash_tbl(icp->ic_htp); */

	del_ctx(QSP_ARG  icp);	// why shouldn't rls_item free the name?  CHECK BUG?
	rls_str((char *)CTX_NAME(icp));

	/* BUG? - perhaps we should make sure
	 * the context is not also pushed deeper down,
	 * to avoid a dangling ptr?
	 */

	/* Now pop the deleted context */

	pop_item_context(QSP_ARG  itp);
}

Item *check_context(Item_Context *icp, const char *name)
{
//#ifdef CAUTIOUS
//	if( icp == NULL ){
//		NERROR1("CAUTIOUS:  check_context:  null icp!?");
//		IOS_RETURN_VAL(NULL)
//	}
//#endif // CAUTIOUS
	assert( icp != NULL );
	return fetch_name(name,CTX_DICT(icp));
}


/*
 * Return a pointer to the item of the given type with the given name,
 * or a null pointer if not found.  Use of this routine does not
 * imply a belief that the named item actually exists.
 *
 * Item types have different "contexts" or name spaces.
 * Originally, when contexts were introduced, there was a single
 * stack that was part of the item type.  But with multi-threading,
 * we would like to have different contexts for different
 * threads.  (That would mean, among other things, that we
 * wouldn't necessarily have to perform mutex locks when accessing
 * different contexts, although it would still be an issue
 * with the global context.)
 *
 * So now there is a tricky question - the context stack has to part
 * of the item type, but we want to have different ones based on the qsp;
 * We don't want to put a context stack for every item type into
 * the query stream...  We don't necessarily need different contexts
 * for every item type.  But for some we do:  one example is script
 * variables, where macros like Assign use scratch script vars...
 * One approach we can try is to use qsp->qs_serial to index an
 * array of context stacks?
 */

Item *item_of( QSP_ARG_DECL  Item_Type *itp, const char *name )
		/* itp = type of item to search for */
		/* name = name of item */
{
	Node *np;

//#ifdef CAUTIOUS
//	check_item_type( itp );
//#endif /* CAUTIOUS */
	assert( itp != NULL );

	if( *name == 0 ) return(NO_ITEM);

	np=QLIST_HEAD(CONTEXT_LIST(itp));

//#ifdef CAUTIOUS
//if(np==NO_NODE ){
//sprintf(ERROR_STRING,"CAUTIOUS:  item type %s has no contexts pushed!?",IT_NAME(itp));
//ERROR1(ERROR_STRING);
//}
//#endif /* CAUTIOUS */
	assert( np != NO_NODE );

	/* check the top context first */

	while(np!=NO_NODE){
		Item_Context *icp;
		Item *ip;

		icp= (Item_Context*) NODE_DATA(np);
if( icp == NULL )
ERROR1("context stack contains null context!?");

//		ip=fetch_name(name,CTX_DICT(icp));
		ip=check_context(icp,name);
		if( ip!=NO_ITEM ){
			return(ip);
		}
		if( IS_RESTRICTED(itp) ){
			// We used to clear the flag here,
			// But now the place that depended on it,
			// (vectree/evaltree.c) has been changed to
			// explicitly clear it.

			//CTX_RSTRCT_FLAG(itp)=0;

			return(NO_ITEM);
		}
		np=NODE_NEXT(np);
	}

	/* not found in any context, including default */

	return(NO_ITEM);
}

/*
 * Return a pointer to the item of the given type with the given name,
 * or a null pointer if not found.  Use of this routine
 * implies a belief that the named item does exist.
 */

Item *get_item( QSP_ARG_DECL  Item_Type *itp, const char* name )
		/* itp = type of item to search for */
		/* name = name of item */
{
	Item *ip;

	ip=item_of(QSP_ARG  itp,name);
	if( ip==NO_ITEM ){
//#ifdef CAUTIOUS
//		check_item_type( itp );
//#endif /* CAUTIOUS */
		assert( itp != NULL );

		sprintf(ERROR_STRING,"no %s \"%s\"",
			IT_NAME(itp),name);
		WARN(ERROR_STRING);
	}
	return(ip);
}

static int item_cmp(const void* ipp1,const void* ipp2)
{
#ifdef __cplusplus
  return strcmp(  ITEM_NAME((*(reinterpret_cast<const Item* const *>(ipp1)))),
  		ITEM_NAME((*(reinterpret_cast<const Item* const *>(ipp2)))) );
#else
  return( strcmp( ITEM_NAME( (*(const Item * const *)ipp1) ),
  		ITEM_NAME( (*(const Item * const *)ipp2) )) );
#endif
}


/*
 * Return a pointer to a list containing the items of the given type,
 * sorted in lexographic order by name.
 *
 * What do we do with contexts?  Do we include everything in the list?
 */

List *item_list(QSP_ARG_DECL  Item_Type *itp)
	/* type of items to list */
{
	Node *np;

//#ifdef CAUTIOUS
//	if( itp == NO_ITEM_TYPE ){
//		WARN("CAUTIOUS:  item_list passed null item type pointer");
//		return(NO_LIST);
//	}
//#endif /* CAUTIOUS */
	assert( itp != NO_ITEM_TYPE );

	/* First check and see if any of the contexts have been updated */

	if( CONTEXT_LIST(itp) != NO_LIST ){
		Node *context_np;
		context_np=QLIST_HEAD(CONTEXT_LIST(itp));
		while(context_np!=NO_NODE){
			Item_Context *icp;
			icp=(Item_Context *) NODE_DATA(context_np);
			/* We can't look just at the dictionary LIST_IS_CURRENT
			 * flag, because if the name space is not using hashing,
			 * then its list is always current, although the context
			 * may have had items added or deleted.
			 */
			if( CTX_FLAGS(icp) & CONTEXT_CHANGED )
				SET_IT_FLAG_BITS(itp, CONTEXT_CHANGED);
			context_np=NODE_NEXT(context_np);
		}
	}

	if( ! NEEDS_NEW_LIST(itp) ){
		/* Nothing changed, just return the existing list */
		return(IT_LIST(itp));
	}

	/* Something has changed, so we have to rebuild the list.
	 * Begin by trashing the old list.
	 */
	
	while( (np=remHead(IT_LIST(itp))) != NO_NODE )
		rls_node(np);

	/* now make up the new list, by concatenating the context lists */
	if( CONTEXT_LIST(itp) != NO_LIST ){
		Node *context_np;
		context_np=QLIST_HEAD(CONTEXT_LIST(itp));
		while(context_np!=NO_NODE){
			Item_Context *icp;
			icp=(Item_Context *) NODE_DATA(context_np);
			cat_dict_items(IT_LIST(itp),CTX_DICT(icp));
			context_np=NODE_NEXT(context_np);
			CLEAR_CTX_FLAG_BITS(icp,CONTEXT_CHANGED);
		}
	}

	CLEAR_IT_FLAG_BITS(itp, (CONTEXT_CHANGED|NEED_LIST) );

	return(IT_LIST(itp));
}

/* reorder a list of items into alphanumeric order of item names */
/* the caller must dispose of the list! */

static List *alpha_sort(QSP_ARG_DECL  List *lp)
{
	short n2sort;
	void **ptr_array;
	Node *np;
	int i;


//#ifdef CAUTIOUS
//	if( lp == NO_LIST ){
//		WARN("CAUTIOUS:  null list passed to alpha_sort");
//		return(NO_LIST);
//	}
//#endif /* CAUTIOUS */
	assert( lp != NO_LIST );

	n2sort=(short)eltcount(lp);

	if( n2sort == 0 ) return(lp);

	ptr_array =(void**)  getbuf( n2sort * sizeof(void *) );

	if( ptr_array == NULL ) {
		NERROR1("make_choices:  out of memory");
		IOS_RETURN_VAL(NULL)
	}
	
	np=QLIST_HEAD(lp);
	i=0;
	while(np!=NO_NODE){
		ptr_array[i++]=NODE_DATA(np);
		np=NODE_NEXT(np);
	}

	/* now sort the pointers */
	qsort(ptr_array,(size_t)n2sort,(size_t)sizeof(char *),item_cmp);

	lp = new_list();
	for(i=0;i<n2sort;i++){
		np=mk_node(ptr_array[i]);
		addTail(lp,np);
	}
	givbuf(ptr_array);
	return(lp);
}

/* BUG this should be gotten from enviroment, termcap, something... */
#define CHARS_PER_LINE	78

/*
 * Print the names of all of the items of the given type to stdout
 */

void list_items(QSP_ARG_DECL  Item_Type *itp)
	/* type of items to list */
{
	List *lp;

	CHECK_ITEM_INDEX(itp)

	lp=item_list(QSP_ARG  itp);
	print_list_of_items(QSP_ARG  lp);
}


/* decap - convert a string from mixed case to all lower case.
 * We do this allow case-insensitive matching.
 */

void decap(char* sto,const char* sfr)
{
	while(*sfr){

		/* use braces in case macro is multiple statements... */
		/* don't increment inside macro ... */
		/* superstitious pc behavior */


		/* BUG should do this properly with autoconf... */

#ifdef SGI	/* or other SYSV os... */
		if( isupper(*sfr) ) { *sto++ = _tolower(*sfr); }

#else		/* sun 4.1.2 */
		if( isupper(*sfr) ) { *sto++ = tolower(*sfr); }
#endif

		else *sto++ = *sfr;

		sfr++;
	}
	*sto = 0;	/* terminate string */
}


/*
 * Find all items of a given type whose names
 * contain a geven substring.
 * Return a pointer to a list of the items,
 * caller must dispose of the list.
 */

List *find_items(QSP_ARG_DECL  Item_Type *itp,const char* frag)
{
	List *lp, *newlp=NO_LIST;
	Node *np, *newnp;
	Item *ip;
	char lc_frag[LLEN];

	lp=item_list(QSP_ARG  itp);
	if( lp == NO_LIST ) return(lp);

	np=lp->l_head;
	decap(lc_frag,frag);
	while(np!=NO_NODE){
		char str1[LLEN];
		ip = (Item*) np->n_data;
		/* make the match case insensitive */
		decap(str1,ip->item_name);
		if( strstr(str1,lc_frag) != NULL ){
			if( newlp == NO_LIST )
				newlp=new_list();
			newnp=mk_node(ip);
			addTail(newlp,newnp);
		}
		np=np->n_next;
	}
	return(newlp);
}


#ifdef NOT_USED
/* Sort the item list based on node priorities (set elsewhere) */

void sort_item_list(QSP_ARG_DECL  Item_Type *itp)
{
	List *lp;

	lp=item_list(QSP_ARG  itp);
	if( lp == NO_LIST ) return;

	p_sort(lp);
}
#endif /* NOT_USED */

void print_list_of_items(QSP_ARG_DECL  List *lp)
{
	Node *np;
	int n_per_line;
	char fmtstr[16];
	int i, n_lines, n_total;
#ifdef HAVE_ISATTY
	FILE *out_fp;
	int maxlen;
#endif /* HAVE_ISATTY */

	/* allocate an array of pointers for sorting */

	if(lp==NO_LIST) return;
	if( (n_total=eltcount(lp)) == 0 ) return;

	lp=alpha_sort(QSP_ARG  lp);
//#ifdef CAUTIOUS
//	if( lp == NO_LIST ){
//		WARN("CAUTIOUS:  no items to print");
//		return;
//	}
//#endif /* CAUTIOUS */
	assert( lp != NO_LIST );

	/* If we are printing to the terminal, then
	 * we want as few lines as possible, but if we're
	 * printing to a file let's have 1 item per line
	 */

#ifdef HAVE_ISATTY
	out_fp = tell_msgfile(SINGLE_QSP_ARG);
	if( isatty( fileno(out_fp) ) ){
		/* find the maximum length */

		np=QLIST_HEAD(lp);
		maxlen=0;
		while(np!=NO_NODE){
			int l;
			if( (l=strlen( ITEM_NAME( ((Item *)NODE_DATA(np)) ) )) > maxlen )
				maxlen=l;
			np=NODE_NEXT(np);
		}
		n_per_line = CHARS_PER_LINE / (maxlen+3);
		if( n_per_line < 1 ) n_per_line=1;
		sprintf(fmtstr,"%%-%ds",maxlen+3);
		n_lines = 1+(n_total-1)/n_per_line;
	} else {
		/* one item per line, nothing fancy */
		/* we still have to count the items */
		n_lines = eltcount(lp);
		n_per_line = 1;
		sprintf(fmtstr,"%%s");
	}

#else /* ! HAVE_ISATTY */


	/* one item per line, nothing fancy */
	/* we still have to count the items */
	n_lines = eltcount(lp);
	n_per_line = 1;
	sprintf(fmtstr,"%%s");

#endif /* ! HAVE_ISATTY */

	/* print the names */

	for(i=0;i<n_lines;i++){
		int j;

		MSG_STR[0]=0;
		for(j=0; j< n_per_line; j++){
			int k;

			k=j*n_lines+i;
			if( k < n_total ){
				char tmp_str[LLEN];

				np = nth_elt(lp,k);
//#ifdef CAUTIOUS
//				if( np==NO_NODE ) {
//					NERROR1("CAUTIOUS:  missing element!?");
//					IOS_RETURN
//				}
				assert( np != NO_NODE );

//if( strlen( ITEM_NAME(((Item *)NODE_DATA(np)) )) > LLEN-1 )
//NERROR1("item name too long");
//#endif /* CAUTIOUS */
				assert( strlen( ITEM_NAME(((Item *)NODE_DATA(np)) )) < LLEN );

				sprintf(tmp_str,fmtstr,
					ITEM_NAME(((Item *)NODE_DATA(np))));
				strcat(MSG_STR,tmp_str);
			}
		}
		prt_msg(MSG_STR);
	}

	/* now free the list */
	dellist(lp);
}


#ifdef NOT_USED

/*
 * Print the hash table statistics for a given class of item
 */

void item_stats(QSP_ARG_DECL  Item_Type * itp)
		/* type of item */
{
	Node *np;

//#ifdef CAUTIOUS
//	check_item_type( itp );
//#endif /* CAUTIOUS */
	assert( itp != NULL );

	np = QLIST_HEAD(CONTEXT_LIST(itp));
	while(np!=NO_NODE){
		Item_Context *icp;
		icp=(Item_Context *) NODE_DATA(np);
		sprintf(MSG_STR,"Context %s:",CTX_NAME(icp));
		prt_msg(MSG_STR);
		tell_name_stats(CTX_DICT(icp));
		np = NODE_NEXT(np);
	}
}
#endif /* NOT_USED */

// used to be called add_to_item_freelist

void recycle_item(Item_Type *itp, void *ip)
{
	Node *np;

//#ifdef CAUTIOUS
//	check_item_type( itp );
//#endif /* CAUTIOUS */
	assert( itp != NULL );

	np=mk_node(ip);
	addTail(IT_FREE_LIST(itp),np);
}

/*
 * Delete the item pointed to by ip from the item database.
 * The actual storage for the item is not freed, but is
 * added to the item type's free list.  The caller must
 * free the stored name to prevent a memory leak.
 */

void del_item(QSP_ARG_DECL  Item_Type *itp,void* ip)
		/* itp = type of item to be deleted */
		/* ip = pointer to item to be deleted */
{
	LOCK_ITEM_TYPE(itp)

//#ifdef CAUTIOUS
//	check_item_type( itp );
//#endif /* CAUTIOUS */
	assert( itp != NULL );

	zombie_item(QSP_ARG  itp,(Item*) ip);
	recycle_item(itp,ip);

	UNLOCK_ITEM_TYPE(itp)
}

/* Set the needy flag for this item type and any classes of which
 * it is a member.
 */

static void make_needy(Item_Type *itp)
{
	SET_IT_FLAG_BITS(itp, NEED_CHOICES | NEED_LIST );

	if( IT_CLASS_LIST(itp) != NO_LIST ){
		Node *np;

		np = QLIST_HEAD(IT_CLASS_LIST(itp));
		while( np != NO_NODE ){
			Item_Class *iclp;

			iclp = (Item_Class*) NODE_DATA(np);
			SET_CL_FLAG_BITS(iclp, NEED_CLASS_CHOICES);
			np = NODE_NEXT(np);
		}
	}
}

/* Remove an item from the item database, but do not return it to the
 * item free list.  This function was introduced to allow image objects
 * to be deleted after they have been displayed in viewers.  The
 * viewers retain a ptr to the data which they need to access in
 * order to refresh the window.
 *
 * This function could have a more descriptive name, like zombieize_item,
 * or hide_item.
 *
 * This is kind of a hack and has a big potential for memory leaks!?
 * It is used in the initial implementation of static objects in vectree,
 * but this is probably not a good idea...  BUG.
 */

void zombie_item(QSP_ARG_DECL  Item_Type *itp,Item* ip)
		/* itp = type of item to be deleted */
		/* ip = pointer to item to be deleted */
{
	Node *np;

//#ifdef CAUTIOUS
//	check_item_type( itp );
//#endif /* CAUTIOUS */
	assert( itp != NULL );

	/* We used to remove the item from the item list here...
	 * but now with the dictionary abstraction, we just remove
	 * it from the the dictionary, and count on item_list()
	 * to detect when the list needs to be updated...
	 */

	np=QLIST_HEAD(CONTEXT_LIST(itp));
	while(np!=NO_NODE){
		Item *tmp_ip;
		Item_Context *icp;

		icp = (Item_Context*) NODE_DATA(np);
		tmp_ip = fetch_name( ITEM_NAME(((Item *)ip)),CTX_DICT(icp));
		if( tmp_ip == ip ){	/* found it */
			if( remove_name(ip,CTX_DICT(icp) ) < 0 ){
				sprintf(ERROR_STRING,
		"zombie_item:  unable to remove %s item %s from context %s",
					IT_NAME(itp),ITEM_NAME(ip),CTX_NAME(icp));
				WARN(ERROR_STRING);
				return;
			}
			/* BUG make the context needy... */
			/* does this do it? */
			SET_CTX_FLAG_BITS(icp, CONTEXT_CHANGED);

			make_needy(itp);
			np=NO_NODE; /* or return? */
		} else
			np=NODE_NEXT(np);
	}
}

/* Rename an item.
 *
 * This function frees the old name and allocates permanent
 * storage for the new name.
 */

void rename_item(QSP_ARG_DECL  Item_Type *itp,void *ip,char* newname)
		/* itp = type of item to be deleted */
{
	Node *np;

	LOCK_ITEM_TYPE(itp)

//#ifdef CAUTIOUS
//	check_item_type( itp );
//#endif /* CAUTIOUS */
	assert( itp != NULL );

	zombie_item(QSP_ARG  itp,(Item*) ip);
	rls_str( (char *) ITEM_NAME(((Item *)ip)) );
	SET_ITEM_NAME( ((Item *)ip), savestr(newname) );
	np=mk_node(ip);
	store_item(QSP_ARG  itp,(Item*) ip,np);

	make_needy(itp);

	UNLOCK_ITEM_TYPE(itp)
}

static void dump_item_context(QSP_ARG_DECL  Item_Context *icp)
{
	sprintf(MSG_STR,"\tContext \"%s\"",CTX_NAME(icp));
	prt_msg(MSG_STR);

	dump_dict_info(CTX_DICT(icp));

	list_item_context(QSP_ARG  icp);
}

void list_item_context(QSP_ARG_DECL  Item_Context *icp)
{
	List *lp;
	lp=dictionary_list(CTX_DICT(icp));
	print_list_of_items(QSP_ARG  lp);
}

/*
 * Print a list of the names of the items of the given type,
 * preceded by the item type name and hash table size.
 */

static void dump_item_type(QSP_ARG_DECL  Item_Type *itp)
{
	Node *np;

	sprintf(MSG_STR,
		"%s items",IT_NAME(itp));
	prt_msg(MSG_STR);

	np = QLIST_HEAD(CONTEXT_LIST(itp));
	while( np != NO_NODE ){
		Item_Context *icp;

		icp = (Item_Context*) NODE_DATA(np);
		dump_item_context(QSP_ARG  icp);

		np = NODE_NEXT(np);
	}
}

/*
 * Dump all existing item types.
 */

void dump_items(SINGLE_QSP_ARG_DECL)
{
	List *lp;
	Node *np;
	Item_Type *itp;

	lp = item_list(QSP_ARG  ittyp_itp);
	if( lp == NO_LIST ) return;
	np=QLIST_HEAD(lp);
	while(np!=NO_NODE){
		itp=(Item_Type *) NODE_DATA(np);
		dump_item_type(QSP_ARG  itp);
		np=NODE_NEXT(np);
	}
}

#ifdef NOT_USED
/* convert a string from mixed case to all lower case.
 * we do this allow case-insensitive matching.
 */

static void decap(char* sto,const char* sfr)
{
	while(*sfr){

		/* use braces in case macro is multiple statements... */
		/* don't increment inside macro ... */
		/* superstitious pc behavior */


#ifdef HAVE_ISUPPER

#ifdef SGI	/* or other SYSV os... */
		if( isupper(*sfr) ) { *sto++ = _tolower(*sfr); }

#else		/* sun 4.1.2 */
		if( isupper(*sfr) ) { *sto++ = tolower(*sfr); }
#endif
		else *sto++ = *sfr;

#else /* ! HAVE_ISUPPER */
		// BUG we could add our own implementation here?
		*sto++ = *sfr;
#endif /* ! HAVE_ISUPPER */

		sfr++;
	}
	*sto = 0;	/* terminate string */
}

/*
 * Find all items of a given type whose names
 * contain a geven substring.
 * Return a pointer to a list of the items,
 * caller must dispose of the list.
 */

static List *find_items(QSP_ARG_DECL  Item_Type *itp,const char* frag)
{
	List *lp, *newlp=NO_LIST;
	Node *np, *newnp;
	Item *ip;
	char lc_frag[LLEN];

	lp=item_list(QSP_ARG  itp);
	if( lp == NO_LIST ) return(lp);

	np=QLIST_HEAD(lp);
	decap(lc_frag,frag);
	while(np!=NO_NODE){
		char str1[LLEN];
		ip = (Item*) NODE_DATA(np);
		/* make the match case insensitive */
		decap(str1,ITEM_NAME(ip));
		if( strstr(str1,lc_frag) != NULL ){
			if( newlp == NO_LIST )
				newlp=new_list();
			newnp=mk_node(ip);
			addTail(newlp,newnp);
		}
		np=NODE_NEXT(np);
	}
	return(newlp);
}
#endif /* NOT_USED */

Item_Type *get_item_type(QSP_ARG_DECL  const char* name)
{
	return( (Item_Type *) get_item(QSP_ARG  ittyp_itp,name) );
}

#ifdef NOT_USED
/*
 * Search all item types for items with matching names
 * BUG? this needs to be tested, may not work...
 */

List *find_all_items(QSP_ARG_DECL  const char* frag)
{
	List *lp, *newlp=NO_LIST;
	List *itlp;
	Node *itnp;

	itlp=item_list(QSP_ARG  ittyp_itp);
	if( itlp == NO_LIST ) return(itlp);
	itnp=QLIST_HEAD(itlp);
	while(itnp!=NO_NODE){
		lp=find_items(QSP_ARG  (Item_Type *)NODE_DATA(itnp),frag);
		if( lp != NO_LIST ){
			if( newlp == NO_LIST )
				newlp=lp;
			else {
				Node *np;

				while( (np=remHead(lp)) != NO_NODE )
					addTail(newlp,np);
				rls_list(lp);
			}
		}
		itnp=NODE_NEXT(itnp);
	}
	return(newlp);
}
#endif /* NOT_USED */

#ifdef THREAD_SAFE_QUERY

#ifdef HAVE_PTHREADS
void report_mutex_error(QSP_ARG_DECL  int status,const char *whence)
{
	const char *msg;

	sprintf(ERROR_STRING,"%s:  report_mutex_error:  status = %d",
		whence,status);
	ADVISE(ERROR_STRING);

	switch(status){
		case EINVAL: msg = "invalid argument"; break;
		case EBUSY: msg = "mutex already locked"; break;
		case EAGAIN: msg = "too many recursive locks"; break;
		case EDEADLK: msg = "lock already owned"; break;
		case EPERM: msg = "not mutex owner"; break;
		default: msg = "unexpected mutex error"; break;
	}
	WARN(msg);
}
#endif /* HAVE_PTHREADS */

#endif /* THREAD_SAFE_QUERY */
