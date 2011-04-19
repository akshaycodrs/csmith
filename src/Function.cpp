// -*- mode: C++ -*-
//
// Copyright (c) 2007, 2008, 2010, 2011 The University of Utah
// All rights reserved.
//
// This file is part of `csmith', a random generator of C programs.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

//
// This file was derived from a random program generator written by Bryan
// Turner.  The attributions in that file was:
//
// Random Program Generator
// Bryan Turner (bryan.turner@pobox.com)
// July, 2005
//
#ifdef WIN32 
#pragma warning(disable : 4786)   /* Disable annoying warning messages */
#endif
#include "Function.h"

#include <cassert>

#include "Common.h"
#include "Block.h"
#include "CGContext.h"
#include "CGOptions.h"
#include "Constant.h"
#include "Effect.h"
#include "Statement.h"
#include "Type.h"
#include "VariableSelector.h"
#include "FactMgr.h"
#include "random.h"
#include "util.h"
#include "Fact.h"
#include "FactPointTo.h"
#include "FactMgr.h"
#include "VectorFilter.h"
#include "Error.h"
#include "DepthSpec.h"
#include "ExtensionMgr.h"
#include "OutputMgr.h"

using namespace std;

///////////////////////////////////////////////////////////////////////////////

static vector<Function*> FuncList;		// List of all functions in the program
static vector<FactMgr*>  FMList;        // list of fact managers for each function
static long cur_func_idx;				// Index into FuncList that we are currently working on
static bool param_first=true;			// Flag to track output of commas 

/*
 * find FactMgr for a function
 */
FactMgr* 
get_fact_mgr_for_func(const Function* func)
{
	for (size_t i=0; i<FuncList.size(); i++) {
		if (FuncList[i] == func) {
			return FMList[i];
		}
	}
    return 0; 
}

/*
 *
 */
FactMgr* 
get_fact_mgr(const CGContext* cg)
{ 
	return get_fact_mgr_for_func(cg->get_current_func());
}

const Function*
find_function_by_name(const string& name)
{
	size_t i;
	for (i=0; i<FuncList.size(); i++) {
		if (FuncList[i]->name == name) {
			return FuncList[i];
		}
	}
	return NULL;
}

int
find_function_in_set(const vector<const Function*>& set, const Function* f)
{
	size_t i;
	for (i=0; i<set.size(); i++) {
		if (set[i] == f) {
			return i;
		}
	}
	return -1;
}

const Block*
find_blk_for_var(const Variable* v) 
{
	if (v->is_global()) {
		return NULL;
	}
	size_t i, j;
	for (i=0; i<FuncList.size(); i++) {
		const Function* func = FuncList[i];
		// for a parameter of a function, pretend it's a variable belongs to the top block
		if (v->is_argument() && find_variable_in_set(func->param, v) != -1) {
			return func->body;
		}
		for (j=0; j<func->blocks.size(); j++) {
			const Block* blk = func->blocks[j];
			if (find_variable_in_set(blk->local_vars, v) != -1) {
				return blk;
			}
		}
	}
	return NULL;
}

/*
 * remove irrelevant facts
 * hint: relevant facts are those concerns variable visible at the beginning of this function
 */
void 
Function::remove_irrelevant_facts(std::vector<const Fact*>& inputs) const
{
	size_t i, j, cnt;
	std::vector<const Fact*> keep_facts;
	size_t len = inputs.size();
	// remove global facts and parameter facts
	for (i=0; i<len; i++) {
		const Variable* v = inputs[i]->get_var();
		if (v->is_global() || find_variable_in_set(param, v) >=0) {
			keep_facts.push_back(inputs[i]);
			inputs.erase(inputs.begin() + i);
			i--;
			len--;
		}
	}
	// find all the facts for variables that might be pointed to by variables we already found.
	// this include variables on stack (most likely locals of callers) but invisible to 
	// this function
	do {
		cnt = keep_facts.size(); 
		for (i=0; i<len; i++) {
			const Fact* f = inputs[i]; 
			// const Variable* v = f->get_var(); 
			for (j=0; j<keep_facts.size(); j++) {
				if (keep_facts[j]->is_relevant(*f)) {
					keep_facts.push_back(f);
					inputs.erase(inputs.begin() + i);
					i--;
					len--;
					break;
				}
			}
		}
	} while (keep_facts.size() > cnt); 
	inputs = keep_facts;
}

bool
Function::is_var_on_stack(const Variable* var, const Statement* stm) const
{  
    size_t i;
    for (i=0; i<param.size(); i++) {
        if (param[i]->match(var)) {
            return true;
        }
    } 
	const Block* b = stm->parent;
	while (b) {
		if (find_variable_in_set(b->local_vars, var) != -1) {
			return true;
		}
		b = b->parent;
	}
    return false;
} 

/* find whether a variable is visible in the given statement
 */
bool
Function::is_var_visible(const Variable* var, const Statement* stm) const
{ 
    return (var->is_global() || is_var_on_stack(var, stm)) ;
} 

/*
 * return true if a variable is out-of-scope at given statement
 * note variable must be a local variable of this function to
 * be a oos variable (sometimes DFA would analyze local variables
 * from callers, they should remain "in-scope" for this function
 */
bool
Function::is_var_oos(const Variable* var, const Statement* stm) const
{
	if (!is_var_visible(var, stm)) { 
		//return true;
		size_t i;
		for (i=0; i<blocks.size(); i++) {
			if (find_variable_in_set(blocks[i]->local_vars, var) != -1) {
				return true;
			}
		}
	}
	return false;
}

const vector<Function*>& 
get_all_functions(void)
{
	return FuncList;
}
 
/*
 *
 */
// unsigned
long
FuncListSize(void)
{
	return FuncList.size();
}

/*
 *
 */
Function *
GetFirstFunction(void)
{
	return FuncList[0];
}

/*
 *
 */
static string
RandomFunctionName(void)
{
	return gensym("func_");
}

/*-------------------------------------------------------------
 *  choose a random return type. only structs and integer types 
 *  (not incl. void)  are qualified, (no arrays)
 *************************************************************/
static const Type*
RandomReturnType(void)
{
	const Type* t = 0; 
        t = Type::choose_random();
	return t;
}

/*
 * Choose a function from `funcs' to invoke.
 * Return null if no suitable function can be found.
 */
static Function *
choose_func(vector<Function *> funcs,
			const CGContext& cg_context,
            const Type* type, 
			const CVQualifiers* qfer)
{
	vector<Function *> ok_funcs;
	vector<Function *>::iterator i;
	const Effect &effect_context = cg_context.get_effect_context();

	for (i = funcs.begin(); i != funcs.end(); ++i) {
        // skip any function which has incompatible return type
        // if type = 0, we don't care
        if (type && !type->is_convertable((*i)->return_type))
            continue;
		if (qfer && (*i)->rv && !qfer->match((*i)->rv->qfer))
			continue;
		// We cannot call a function that has an as-yet unknown effect.
		// TODO: in practice, this means we never call an as-yet unbuilt func.
		if ((*i)->is_effect_known() == false) {
			continue;
		}
		// We cannot call a function with a side-effect if the current context
		// already has a side-effect.
		// TODO: is this too strong for what we want?
		if (!((*i)->get_feffect().is_side_effect_free())
			&& !effect_context.is_side_effect_free()) {
			continue;
		}
		// We cannot call a function that has a race with the current context.
		if ((*i)->get_feffect().has_race_with(effect_context)) {
			continue;
		}
		// Otherwise, this is an acceptable choice.
		ok_funcs.push_back(*i);
	}
	
	vector<Function *>::size_type ok_size = ok_funcs.size();
	
	if (ok_size == 0) {
		return 0;
	}
	if (ok_size == 1) {
		return ok_funcs[0];
	}
	int index = rnd_upto(ok_size);
	ERROR_GUARD(NULL);
	return ok_funcs[index];
}

/*
 * WARNING: this function may return null!
 */
Function *
SelectFunction(bool &isBackLink, CGContext &cg_context, const Type* type, const CVQualifiers* qfer)
{
	vector<Function*> available_funcs = FuncList; 
	// Select between creating a new function, or calling an old function.
	if ((FuncListSize() < CGOptions::max_funcs())
		&& (available_funcs.empty() || rnd_flipcoin(50))) {
		ERROR_GUARD(NULL);
		isBackLink = false;
		return Function::make_random(cg_context, type, qfer);
		
	} else {
		// Try to link to a previously defined function.
		// (This may cause cycles in the call graph!)
		// TODO: get rid of recursive calls
		Function *callee = choose_func(available_funcs, cg_context, type, qfer);
		ERROR_GUARD(NULL);
		if (!callee) {
			// TODO: Maybe should generate a new function?
			// for function with return type of struct, we have to create them
			// as the alternative unary/binary "functions" don't produce struct
			// pointers are not produced by unary/binary operations for now.
			if ((FuncListSize() < CGOptions::max_funcs()) && type && (type->eType == eStruct || type->eType == ePointer)) {
				isBackLink = false;
				return Function::make_random(cg_context, type, qfer);
			}
			return 0;
		} 
		// unsigned long idx = rnd_upto(FuncList.size());
		// isBackLink = (idx <= cur_func_idx);
		isBackLink = true; // XXX
		return callee; 
	}
}

/*
 *
 */
static unsigned int
ParamListProbability()
{
	return rnd_upto(CGOptions::max_params());
}

/*
 *
 */
static void
GenerateParameterList(Function &curFunc)
{
	unsigned int max = ParamListProbability();
	ERROR_RETURN();

	for (unsigned int i =0; i <= max; i++) {
		// With some probability, choose a new random variable, or one from
		// parentParams, parentLocals, or the globals list.
		//
		// Also, build the parent's link structure for this invocation and push
		// it onto the back.
		//
		VariableSelector::GenerateParameterVariable(curFunc);
		ERROR_RETURN();
	}
}

/*
 *
 */
Function::Function(const string &name, const Type *return_type)
	: name(name),
	  return_type(return_type),
	  body(0),
	  fact_changed(false),
	  visited_cnt(0),
	  build_state(UNBUILT)
{
	FuncList.push_back(this);			// Add to global list of functions.
}

/*
 *
 */
Function *
Function::make_random(const CGContext& cg_context, const Type* type, const CVQualifiers* qfer)
{
	if (type == 0)
        	type = RandomReturnType();

	DEPTH_GUARD_BY_TYPE_RETURN(dtFunction, NULL);
	ERROR_GUARD(NULL);
	Function *f = new Function(RandomFunctionName(), type);
		
	// dummy variable representing return variable, we don't care about the type, so use 0
	string rvname = f->name + "_" + "rv";
	CVQualifiers ret_qfer = qfer==0 ? CVQualifiers::random_qualifiers(type, Effect::READ, cg_context, true) 
		                            : qfer->random_qualifiers(true, Effect::READ, cg_context);
	ERROR_GUARD(NULL);
	f->rv = VariableSelector::make_dummy_variable(rvname, &ret_qfer); 

	// create a fact manager for this function, with global facts copied from parent
	FactMgr* parent_fm = get_fact_mgr(&cg_context);
	FMList.push_back(new FactMgr(f, parent_fm->global_facts));

	// Select a set of vars to operate on.
	GenerateParameterList(*f);
	ERROR_GUARD(NULL);
	f->GenerateBody(cg_context);
	ERROR_GUARD(NULL);
	return f;
}

/*
 *
 */
Function *
Function::make_first(void)
{
	const Type *ty = RandomReturnType();
	ERROR_GUARD(NULL);

	Function *f = new Function(RandomFunctionName(), ty);
	// dummy variable representing return variable, we don't care about the type, so use 0
	string rvname = f->name + "_" + "rv"; 
	CVQualifiers ret_qfer = CVQualifiers::random_qualifiers(ty); 
	ERROR_GUARD(NULL);
	f->rv = VariableSelector::make_dummy_variable(rvname, &ret_qfer);

	// create a fact manager for this function, with empty global facts 
	FactMgr* fm = new FactMgr(f);
	FMList.push_back(fm);

	ExtensionMgr::GenerateFirstParameterList(*f);

	// No Parameter List
	f->GenerateBody(CGContext::get_empty_context());
	fm->setup_in_out_maps(true);

	// collect info about global dangling pointers
	size_t i;
	for (i=0; i<fm->global_facts.size(); i++) { 
		const Variable* v = fm->global_facts[i]->get_var(); 
		// const pointers should never be dangling
		if (v->is_const() || !v->is_global()) continue;  
		if (fm->global_facts[i]->eCat == ePointTo) {
			FactPointTo* fp = (FactPointTo*)(fm->global_facts[i]);
			if (fp->is_dead()) {
				f->dead_globals.push_back(v);
			}
		}
	}
	return f;
}

/*
 *
 */
static int
OutputFormalParam(Variable *var, std::ostream *pOut)
{
	std::ostream &out = *pOut;
	if ( !param_first ) out << ", ";
	param_first = false;
	//var->type->Output( out );
	if (!CGOptions::arg_structs() && var->type)
		assert(var->type->eType != eStruct);
		
	var->output_qualified_type(out);
	out << " " << var->name;
    return 0;
}

/*
 *
 */
void
Function::OutputFormalParamList(std::ostream &out)
{
	if (param.size() == 0) {
		assert(Type::void_type);
		Type::void_type->Output(out);
	} else {
		param_first = true;
		for_each(param.begin(),
				 param.end(),
				 std::bind2nd(std::ptr_fun(OutputFormalParam), &out));
	}
}

/*
 *
 */
void
Function::OutputHeader(std::ostream &out)
{
	if (!CGOptions::return_structs() && return_type)
		assert(return_type->eType != eStruct);
	rv->qfer.output_qualified_type(return_type, out);
	out << " " << get_prefixed_name(name) << "(";
	OutputFormalParamList( out );
	out << ")";
}

/*
 *
 */
void
Function::OutputForwardDecl(std::ostream &out)
{
	OutputHeader(out);
	out << ";";
	outputln(out);
}

/*
 *
 */
void
Function::Output(std::ostream &out)
{
	OutputMgr::set_curr_func(name);
	output_comment_line(out, "------------------------------------------");
	if (!CGOptions::concise()) {
		feffect.Output(out);
	}
	OutputHeader(out);
	outputln(out);

	if (CGOptions::depth_protect()) {
		out << "if (DEPTH < MAX_DEPTH) ";
		outputln(out);
	}

	FactMgr* fm = get_fact_mgr_for_func(this);
	// if nothing interesting happens, we don't want to see facts for statements
	if (!fact_changed && !is_pointer_referenced()) {
		fm = 0;
	}
	body->Output(out, fm);

	if (CGOptions::depth_protect()) {
		out << "else";
		outputln(out);

		// TODO: Needs to be fixed when return types are no longer simple
		// types.

		out << "return ";
		ret_c->Output(out);
		out << ";";
		outputln(out);
	}

	outputln(out);
	outputln(out);
}

/*
 * Used for protecting depth
 */
void
Function::make_return_const()
{
	if (CGOptions::depth_protect() && need_return_stmt()) {
		assert(return_type);
		if (return_type->eType == eSimple)
			assert(return_type->simple_type != eVoid);
		Constant *c = Constant::make_random(return_type);
		ERROR_RETURN();		
		this->ret_c = c;
	}
}

bool Function::need_return_stmt()
{
	return (return_type->eType != eSimple || return_type->simple_type != eVoid);
}

/*
 *
 */
void
Function::GenerateBody(const CGContext &prev_context)
{
	if (build_state != UNBUILT) {
		cerr << "warning: ignoring attempt to regenerate func" << endl;
		return;
	}

	build_state = BUILDING;
	Effect effect_accum; 
	CGContext cg_context(this, prev_context.get_effect_context(), &effect_accum);
	cg_context.extend_call_chain(prev_context);
	FactMgr* fm = get_fact_mgr_for_func(this);
	for (size_t i=0; i<param.size(); i++) {
		if (param[i]->type->ptr_type != 0) {
			fm->global_facts.push_back(FactPointTo::make_fact(param[i], FactPointTo::tbd_ptr));
		}
	}
	// Fill in the Function body. 
	body = Block::make_random(cg_context); 
	ERROR_RETURN();
	body->set_depth_protect(true);

	// compute the pointers that are statically referenced in the function
	// including ones referenced by its callees
	body->get_referenced_ptrs(referenced_ptrs);

	// Compute the function's externally visible effect.  Currently, this
	// is just the effect on globals.
	//effect.add_external_effect(*cg_context.get_effect_accum());
	feffect.add_external_effect(fm->map_stm_effect[body]);
	
	make_return_const();
	ERROR_RETURN();
	
	// Mark this function as built.
	build_state = BUILT;
}

/*
 *
 */
void
GenerateFunctions(void)
{ 
	FactMgr::add_interested_facts(CGOptions::interested_facts());
	// -----------------
	// Create a basic first function, then generate a random graph from there.
	/* Function *first = */ Function::make_first();
	ERROR_RETURN();
	
	// -----------------
	// Create body of each function, continue until no new functions are created.
	for (cur_func_idx = 0; cur_func_idx < FuncListSize(); cur_func_idx++) {
		// Dynamically adds new functions to the end of the list..
		if (FuncList[cur_func_idx]->is_built() == false) {
			FuncList[cur_func_idx]->GenerateBody(CGContext::get_empty_context());
			ERROR_RETURN();
		}
	}
	FactPointTo::aggregate_all_pointto_sets();

	ExtensionMgr::GenerateValues();
}

/*
 *
 */
static int
OutputForwardDecl(Function *func, std::ostream *pOut)
{
	func->OutputForwardDecl(*pOut);
    return 0;
}

/*
 *
 */
static int
OutputFunction(Function *func, std::ostream *pOut)
{
	func->Output(*pOut);
    return 0;
}

/*
 *
 */
void
OutputForwardDeclarations(std::ostream &out)
{
	outputln(out);
	outputln(out);
	output_comment_line(out, "--- FORWARD DECLARATIONS ---");
	for_each(FuncList.begin(), FuncList.end(),
			 std::bind2nd(std::ptr_fun(OutputForwardDecl), &out));
}

/*
 *
 */
void
OutputFunctions(std::ostream &out)
{
	outputln(out);
	outputln(out);
	output_comment_line(out, "--- FUNCTIONS ---");
	for_each(FuncList.begin(), FuncList.end(),
			 std::bind2nd(std::ptr_fun(OutputFunction), &out));
}

/*
 * Delete a single function
 */
int
Function::deleteFunction(Function* func)
{
	if (func) {
		delete func;
		func = 0;
	}
	return 0;
}

/*
 * Release all dynamic memory
 */
void
Function::doFinalization(void)
{
	for_each(FuncList.begin(), FuncList.end(), std::ptr_fun(deleteFunction));

	FuncList.clear();

	std::vector<FactMgr*>::iterator i;
	for (i = FMList.begin(); i != FMList.end(); ++i) {
		delete (*i);
	}
	FMList.clear();
	FactMgr::doFinalization();
}

Function::~Function()
{
	param.clear();

	assert(stack.empty());

	if (body) {
		delete body;
		body = NULL;
	}

	if (CGOptions::depth_protect() && ret_c) {
		delete ret_c;
		ret_c = NULL;
	}
}


///////////////////////////////////////////////////////////////////////////////

// Local Variables:
// c-basic-offset: 4
// tab-width: 4
// End:

// End of file.