#include "js.h"
#include "stack.h"
#include "util.h"
#include "memory.h"
#include "js_value.h"
#include "error.h"
#include <string.h>
#include "expression.h"
#include "interprete.h"

int get_expression_list_length(ExpressionList *list)
{
	int length = 0;
	while (NULL != list)
	{
		list = list->next;
		length++;
	}
	return length;
};

int eval_negative_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	eval_expression(inter, env, e->u.unary);
	JsValue v = pop_stack(&inter->stack);
	v = js_negative(&v); /*write value back*/
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_increment_decrement_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsValue *left = get_left_value(inter, env, e->u.unary);
	if (NULL == left)
	{
		ERROR_runtime_error(RUNTIME_ERROR_VARIABLE_NOT_FOUND, 
			"variable not defined or can not use as left value", e->line);
		return RUNTIME_ERROR_VARIABLE_NOT_FOUND;
	}
	JsValue oldvalue = *left;
	if (EXPRESSION_TYPE_INCREMENT == e->typ || EXPRESSION_TYPE_PRE_INCREMENT == e->typ)
	{
		*left = js_increment_or_decrement(left, 1);
	}
	else
	{
		*left = js_increment_or_decrement(left, 0);
	}
	if (EXPRESSION_TYPE_PRE_DECREMENT == e->typ || EXPRESSION_TYPE_PRE_INCREMENT == e->typ)
	{
		push_stack(&inter->stack, left);
	}
	else
	{
		push_stack(&inter->stack, &oldvalue);
	}
	return 0;
}

int eval_logical_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsValue v;
	v.typ = JS_VALUE_TYPE_BOOL;
	eval_expression(inter, env, e->u.binary->left);
	JsValue left = pop_stack(&inter->stack);
	v.u.boolvalue = is_js_value_true(&left);
	if (JS_BOOL_FALSE == v.u.boolvalue && EXPRESSION_TYPE_LOGICAL_AND == e->typ)
	{
		push_stack(&inter->stack, &v);
		return 0;
	}
	if (JS_BOOL_TRUE == v.u.boolvalue && EXPRESSION_TYPE_LOGICAL_OR == e->typ)
	{
		push_stack(&inter->stack, &v);
		return 0;
	}
	eval_expression(inter, env, e->u.binary->right);
	JsValue right = pop_stack(&inter->stack);
	JSBool second = is_js_value_true(&right);
	if (JS_BOOL_TRUE == v.u.boolvalue && EXPRESSION_TYPE_LOGICAL_AND == e->typ)
	{
		if (JS_BOOL_FALSE == second)
		{
			v.u.boolvalue = JS_BOOL_FALSE;
		}
	}
	if (JS_BOOL_FALSE == v.u.boolvalue && EXPRESSION_TYPE_LOGICAL_OR == e->typ)
	{
		if (JS_BOOL_TRUE == second)
		{
			v.u.boolvalue = JS_BOOL_TRUE;
		}
	}
	push_stack(&inter->stack, &v);
}

int eval_string_expression(JsInterpreter *inter, Expression *e)
{
	JsValue v;
	v.typ = JS_VALUE_TYPE_STRING_LITERAL;
	v.u.literal_string = e->u.string;
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_arithmetic_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsValue v;
	eval_expression(inter, env, e->u.binary->right);
	eval_expression(inter, env, e->u.binary->left);
	JsValue left = pop_stack(&inter->stack);
	JsValue right = pop_stack(&inter->stack);
	if (EXPRESSION_TYPE_MUL == e->typ)
	{
		v = js_value_mul(&left, &right);
	}
	if (EXPRESSION_TYPE_MOD == e->typ)
	{
		v = js_value_mod(&left, &right);
	}
	if (EXPRESSION_TYPE_DIV == e->typ)
	{
		v = js_value_div(&left, &right);
	}
	if (EXPRESSION_TYPE_SUB == e->typ)
	{
		v = js_value_sub(&left, &right);
	}
	if (EXPRESSION_TYPE_ADD == e->typ)
	{
		v = js_value_add(inter, &left, &right, e->line);
	}
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_relation_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsValue v;
	v.typ = JS_VALUE_TYPE_BOOL;
	v.u.boolvalue = JS_BOOL_FALSE;
	eval_expression(inter, env, e->u.binary->left);
	eval_expression(inter, env, e->u.binary->right);
	JsValue right = pop_stack(&inter->stack);
	JsValue left = pop_stack(&inter->stack);
	if (EXPRESSION_TYPE_EQ == e->typ)
	{
		v.u.boolvalue = js_value_equal(&left, &right);
	}
	if (EXPRESSION_TYPE_NE == e->typ)
	{
		v.u.boolvalue = js_value_equal(&left, &right);
		if (JS_BOOL_FALSE == v.u.boolvalue)
		{
			v.u.boolvalue = JS_BOOL_TRUE;
		}
		else
		{
			v.u.boolvalue = JS_BOOL_FALSE;
		}
	}
	if (EXPRESSION_TYPE_GE == e->typ)
	{
		v.u.boolvalue = js_value_greater_or_equal(&left, &right);
	}
	if (EXPRESSION_TYPE_LE == e->typ)
	{
		v.u.boolvalue = js_value_greater_or_equal(&right, &left);
	}
	if (EXPRESSION_TYPE_GT == e->typ)
	{
		v.u.boolvalue = js_value_greater(&left, &right);
	}
	if (EXPRESSION_TYPE_LT == e->typ)
	{
		v.u.boolvalue = js_value_greater(&right, &left);
	}
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_self_op_assign_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	eval_expression(inter, env, e->u.binary->right); /*get assign value*/
	JsValue value = pop_stack(&inter->stack);
	JsValue *dest = get_left_value(inter, env, e->u.binary->left);
	if (NULL == dest)
	{
		ERROR_runtime_error(RUNTIME_ERROR_VARIABLE_NOT_FOUND, "", e->line);
		return RUNTIME_ERROR_VARIABLE_NOT_FOUND;
	}
	JsValue newvalue;
	switch (e->typ)
	{
	case EXPRESSION_TYPE_PLUS_ASSIGN:
		newvalue = js_value_add(inter, dest, &value, e->line);
		break;
	case EXPRESSION_TYPE_MINUS_ASSIGN:
		newvalue = js_value_sub(dest, &value);
		break;
	case EXPRESSION_TYPE_MUL_ASSIGN:
		newvalue = js_value_mul(dest, &value);
		break;
	case EXPRESSION_TYPE_DIV_ASSIGN:
		newvalue = js_value_div(dest, &value);
		break;
	case EXPRESSION_TYPE_MOD_ASSIGN:
		newvalue = js_value_mod(dest, &value);
		break;
	}
	*dest = newvalue;
	push_stack(&inter->stack, dest);
	return 0;
}

int eval_assign_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	eval_expression(inter, env, e->u.binary->right); /*get assign value*/
	JsValue value = pop_stack(&inter->stack);
	JsValue *dest = get_left_value(inter, env, e->u.binary->left);
	if (NULL == dest)
	{
		ERROR_runtime_error(RUNTIME_ERROR_VARIABLE_NOT_FOUND, "", e->line);
		return RUNTIME_ERROR_VARIABLE_NOT_FOUND;
	}
	if (JS_VALUE_TYPE_STRING_LITERAL == value.typ)
	{
		int length = strlen(value.u.literal_string);
		JsString *string = INTERPRETER_create_heap(inter, JS_VALUE_TYPE_STRING, length + 1, e->line);
		strncpy(string->s, value.u.literal_string, length);
		string->s[length] = 0;
		string->length = length;
		JsValue newv;
		newv.typ = JS_VALUE_TYPE_STRING;
		newv.u.string = string;
		*dest = newv;
	}
	else
	{
		*dest = value;
	}
	extern gc_sweep_should_executing;
	if (1 == gc_sweep_should_executing)
	{
		gc_mark(env);
		gc_sweep(inter);
		gc_sweep_should_executing = 0;
	}
	push_stack(&inter->stack, dest);
	return 0;
}

int eval_array_index_expression(JsInterpreter *inter, ExecuteEnvironment *env, JsValue *array, ExpressionIndex *index, int line)
{
	JsValue key;
	JsArray *arr = array->u.array;
	if (INDEX_TYPE_EXPRESSION == index->typ)
	{
		eval_expression(inter, env, index->index);
		key = pop_stack(&inter->stack);
		if (JS_VALUE_TYPE_INT != key.typ)
		{
			ERROR_runtime_error(RUNTIME_ERROR_INDEX_HAS_WRONG_TYPE, "array index must be int", line);
			return RUNTIME_ERROR_INDEX_HAS_WRONG_TYPE;
		}
		if (key.u.intvalue < 0 || key.u.intvalue >= arr->length)
		{
			ERROR_runtime_error(RUNTIME_ERROR_INDEX_OUT_RANGE, "", line);
			return RUNTIME_ERROR_INDEX_OUT_RANGE;
		}
		push_stack(&inter->stack, arr->elements + key.u.intvalue);
		return 0;
	}

	/* type == IDENTIFIER*/
	JsValue v;

	if (0 == strcmp("length", index->identifier))
	{
		v.typ = JS_VALUE_TYPE_INT;
		v.u.intvalue = arr->length;
		push_stack(&inter->stack, &v);
		return 0;
	}

	ERROR_runtime_error(RUNTIME_ERROR_FIELD_NOT_DEFINED, index->identifier, line);

	return RUNTIME_ERROR_FIELD_NOT_DEFINED;
}

int eval_index_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	eval_expression(inter, env, e->u.index->e);
	JsValue v = pop_stack(&inter->stack);
	ExpressionIndex *index = e->u.index;
	if (JS_VALUE_TYPE_ARRAY == v.typ)
	{ /*handle array part*/
		return eval_array_index_expression(inter, env, &v, index, e->line);
	}

	if (JS_VALUE_TYPE_OBJECT != v.typ)
	{
		ERROR_runtime_error(RUNTIME_ERROR_CANNOT_INDEX_THIS_TYPE, "not a array and not a object", e->line);
		return RUNTIME_ERROR_CANNOT_INDEX_THIS_TYPE;
	}

	JsValue *value = NULL;
	if (INDEX_TYPE_IDENTIFIER == index->typ)
	{
		value = (JsValue *)INTERPRETER_search_field_from_object_include_prototype(v.u.object, index->identifier);
	}
	else
	{ /*index_type_expression*/
		eval_expression(inter, env, e->u.index->index);
		JsValue key = pop_stack(&inter->stack);
		if (JS_VALUE_TYPE_STRING == key.typ)
		{
			value = INTERPRETER_search_field_from_object_include_prototype(v.u.object, *key.u.string->s);
		}
		if (JS_VALUE_TYPE_STRING_LITERAL == key.typ)
		{
			value = INTERPRETER_search_field_from_object_include_prototype(v.u.object, key.u.literal_string);
		}
	}
	if (NULL == value)
	{
		ERROR_runtime_error(RUNTIME_ERROR_FIELD_NOT_DEFINED, "not found", e->line);
		return RUNTIME_ERROR_FIELD_NOT_DEFINED;
	}
	push_stack(&inter->stack, value);
	return 0;
}

int eval_array_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	int length = get_expression_list_length(e->u.expression_list);
	JsValue v;
	v.typ = JS_VALUE_TYPE_ARRAY;
	JsArray *array = INTERPRETER_create_heap(inter, JS_VALUE_TYPE_ARRAY, length * 2 + 1, e->line);
	v.u.array = array;
	array->length = 0;
	ExpressionList *list = e->u.expression_list;
	JsValue vv;
	while (NULL != list)
	{
		eval_expression(inter, env, list->expression);
		vv = pop_stack(&inter->stack);
		array->elements[array->length] = vv;
		array->length++;
		list = list->next;
	}
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_method_and_function_call(
	JsInterpreter *inter,
	ExecuteEnvironment *env,
	JsObject *object,
	JsFunction *func,
	ArgumentList *args,
	int line)
{
	ExecuteEnvironment *callenv = INTERPRETER_alloc_env(inter, env, line);
	ExecuteEnvironment *closureenv = NULL;
	if (NULL == object)
	{
		object = INTERPRETER_create_heap(inter, JS_VALUE_TYPE_OBJECT, 0, line);
		closureenv = get_last_not_null_outter_env(func->env);
		if (NULL != closureenv)
		{
			callenv->outter = func->env;
			closureenv->outter = env;
		}
	}
	else
	{
		closureenv = get_last_not_null_outter_env(object->env);
		if (NULL != closureenv)
		{
			callenv->outter = object->env;
			closureenv->outter = env;
		}
	}
	ParameterList *paras = func->parameter_list;
	JsValue v;
	v.typ = JS_VALUE_TYPE_NULL;
	int args_count = get_expression_list_length(args);

	JsValue arguments;
	JsArray *arguments_arr = INTERPRETER_create_heap(inter, JS_VALUE_TYPE_ARRAY, args_count, line);
	arguments.typ = JS_VALUE_TYPE_ARRAY;
	arguments.u.array = arguments_arr;
	while (NULL != args)
	{
		/*make value*/
		eval_expression(inter, env, args->expression);
		v = pop_stack(&inter->stack);
		if (NULL != paras)
		{
			INTERPRETER_create_variable(inter, callenv, paras->identifier, &v, line);
			paras = paras->next;
		}
		arguments_arr->elements[arguments_arr->length] = v;
		arguments_arr->length++;
		args = args->next;
	}
	v.typ = JS_VALUE_TYPE_NULL;
	while (NULL != paras)
	{ /*args are more than paras,no big deal*/
		INTERPRETER_create_variable(inter, callenv, paras->identifier, &v, line);
		paras = paras->next;
	}
	INTERPRETER_create_variable(inter, callenv, "arguments", &arguments, line);
	JsValue this;
	this.typ = JS_VALUE_TYPE_OBJECT;
	this.u.object = object;
	INTERPRETER_create_variable(inter, callenv, "this", &this, line);
	StatementList *list = func->block->list;
	StatementResult ret;
	while (NULL != list)
	{
		ret = INTERPRETER_execute_statement(inter, callenv, list->statement);
		switch (ret.typ)
		{
		case STATEMENT_RESULT_TYPE_NORMAL:
			break; /*nothing to do*/
		case STATEMENT_RESULT_TYPE_CONTINUE:
			INTERPRETER_free_env(inter, callenv);
			ERROR_runtime_error(RUNTIME_ERROR_CONTINUE_RETURN_BREAK_CAN_NOT_BE_IN_THIS_SCOPE, "continue", list->statement->line);
			return RUNTIME_ERROR_CONTINUE_RETURN_BREAK_CAN_NOT_BE_IN_THIS_SCOPE;
		case STATEMENT_RESULT_TYPE_BREAK:
			INTERPRETER_free_env(inter, callenv);
			ERROR_runtime_error(RUNTIME_ERROR_CONTINUE_RETURN_BREAK_CAN_NOT_BE_IN_THIS_SCOPE, "break", list->statement->line);
			return RUNTIME_ERROR_CONTINUE_RETURN_BREAK_CAN_NOT_BE_IN_THIS_SCOPE;
		case STATEMENT_RESULT_TYPE_RETURN:
			goto funcend;
		}
		list = list->next;
	}
funcend:
	callenv->outter = env;
	if (NULL != closureenv)
	{
		closureenv->outter = NULL;
	}
	if (STATEMENT_RESULT_TYPE_RETURN != ret.typ)
	{ /*push a default value*/
		v.typ = JS_VALUE_TYPE_NULL;
		push_stack(&inter->stack, &v);
		INTERPRETER_free_env(inter, callenv);
	}
	else
	{
		INTERPRETER_check_return_value_free_env_or_push_in_envheap(inter, callenv, &ret);
	}
	return 0;
}

int eval_function_call_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	/*only support search global function now!!*/
	JsFunction *func = NULL;
	if (NULL != e->u.function_call->func)
	{
		func = INTERPRETER_search_func_from_env(env, e->u.function_call->func);
	}
	else
	{
		eval_expression(inter, env, e->u.function_call->e);
		JsValue v = pop_stack(&inter->stack);
		if (JS_VALUE_TYPE_FUNCTION != v.typ)
		{
			ERROR_runtime_error(RUNTIME_ERROR_NOT_A_FUNCTION, "", e->line);
			return RUNTIME_ERROR_NOT_A_FUNCTION;
		}
		func = v.u.func;
	}
	if (NULL == func)
	{
		ERROR_runtime_error(RUNTIME_ERROR_FUNCTION_NOT_FOUND, e->u.function_call->func, e->line);
		return RUNTIME_ERROR_FUNCTION_NOT_FOUND;
	}
	if (JS_FUNCTION_TYPE_BUILDIN == func->typ)
	{
		/*execute build in function*/
		return eval_build_in_function(inter, env, func->buildin, e->u.function_call->args);
	}

	return eval_method_and_function_call(inter, env, NULL, func, e->u.function_call->args, e->line);
}

int eval_object_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{

	JsValue v;
	v.typ = JS_VALUE_TYPE_OBJECT;
	v.u.object = INTERPRETER_create_heap(inter, JS_VALUE_TYPE_OBJECT, 0, e->line);
	ExpressionObjectKVList *list = e->u.object_kv_list;
	JsValue value;
	while (NULL != list)
	{
		if (NULL != list->kv->identifier_key)
		{
			if (NULL != list->kv->value)
			{
				eval_expression(inter, env, list->kv->value);
				value = pop_stack(&inter->stack);
			}
			else
			{
				value.typ = JS_VALUE_TYPE_FUNCTION;
				value.u.func = list->kv->func;
			}
			INTERPRETE_create_object_field(inter, v.u.object, list->kv->identifier_key, &value, list->kv->line);
		}
		else
		{ /*expression*/
			eval_expression(inter, env, list->kv->expression_key);
			JsValue key = pop_stack(&inter->stack);
			if (JS_VALUE_TYPE_STRING_LITERAL != key.typ && JS_VALUE_TYPE_STRING != key.typ)
			{
				ERROR_runtime_error(RUNTIME_ERROR_INDEX_HAS_WRONG_TYPE, "only string can be used as object key", list->kv->expression_key->line);
				return RUNTIME_ERROR_INDEX_HAS_WRONG_TYPE;
			}
			if (NULL != list->kv->value)
			{
				eval_expression(inter, env, list->kv->value);
				value = pop_stack(&inter->stack);
			}
			else
			{
				value.typ = JS_VALUE_TYPE_FUNCTION;
				value.u.func = list->kv->func;
			}
			if (JS_VALUE_TYPE_STRING_LITERAL == key.typ)
			{
				INTERPRETE_create_object_field(inter, v.u.object, key.u.literal_string, &value, list->kv->value->line);
			}
			else
			{
				INTERPRETE_create_object_field(inter, v.u.object, key.u.string->s, &value, list->kv->value->line);
			}
		}
		list = list->next;
	}
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_new_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	ExpressionNew *new = e->u.new;
	if (0 == strcmp("Object", new->identifier))
	{
		JsValue v;
		v.typ = JS_VALUE_TYPE_OBJECT;
		v.u.object = INTERPRETER_create_heap(inter, JS_VALUE_TYPE_OBJECT, 0, e->line);
		;
		push_stack(&inter->stack, &v);
		return 0;
	}
	if (0 == strcmp("Array", new->identifier))
	{
		Expression arraye;
		arraye.u.expression_list = new->args;
		return eval_array_expression(inter, env, &arraye);
	}
	ERROR_runtime_error(RUNTIME_ERROR_UNKOWN_NEW_TYPE, new->identifier, e->line);
	return RUNTIME_ERROR_UNKOWN_NEW_TYPE;
}

int eval_assign_function_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	ExpressionAssignFunction *assign = e->u.assign_function;

	Expression *left_value_expression = assign->dest;
	if (NULL == left_value_expression)
	{
		Expression identifier;
		identifier.typ = EXPRESSION_TYPE_IDENTIFIER;
		identifier.u.identifier = assign->identifier;
		left_value_expression = &identifier;
	}

	JsValue *left = get_left_value(inter, env, left_value_expression);
	if (NULL == left)
	{
		ERROR_runtime_error(RUNTIME_ERROR_VARIABLE_NOT_FOUND, "", e->line);
		return RUNTIME_ERROR_VARIABLE_NOT_FOUND;
	}
	left->typ = JS_VALUE_TYPE_FUNCTION;
	left->u.func = assign->func;
	push_stack(&inter->stack, left);
	return 0;
}

int eval_create_function_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsFunction *func = e->u.func;
	INTERPRETE_create_function(inter, env, func, e->line);
	JsValue v;
	v.typ = JS_VALUE_TYPE_FUNCTION;
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_not_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	eval_expression(inter, env, e->u.unary);
	JsValue v = pop_stack(&inter->stack);
	if (JS_VALUE_TYPE_BOOL == v.typ)
	{
		v.u.boolvalue = js_reverse_bool(v.u.boolvalue);
	}
	else
	{
		JSBool is_true = js_reverse_bool(is_js_value_true(&v));
		v.typ = JS_VALUE_TYPE_BOOL;
		v.u.boolvalue = is_true;
	}
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsValue v;
	/*bool expression*/
	switch (e->typ)
	{
	case EXPRESSION_TYPE_BOOL:
		v.typ = JS_VALUE_TYPE_BOOL;
		v.u.boolvalue = e->u.bool_value;
		push_stack(&inter->stack, &v);
		break;
	case EXPRESSION_TYPE_INT:
		v.typ = JS_VALUE_TYPE_INT;
		v.u.intvalue = e->u.int_value;
		push_stack(&inter->stack, &v);
		break;
	case EXPRESSION_TYPE_FLOAT:
		v.typ = JS_VALUE_TYPE_FLOAT;
		v.u.floatvalue = e->u.double_value;
		push_stack(&inter->stack, &v);
		break;
	case EXPRESSION_TYPE_NULL:
		v.typ = JS_VALUE_TYPE_NULL;
		push_stack(&inter->stack, &v);
		break;
	case EXPRESSION_TYPE_UNDEFINED:
		v.typ = JS_VALUE_TYPE_UNDEFINED;
		push_stack(&inter->stack, &v);
		break;
	case EXPRESSION_TYPE_ASSIGN:
		return eval_assign_expression(inter, env, e);
	case EXPRESSION_TYPE_PLUS_ASSIGN:
	case EXPRESSION_TYPE_MINUS_ASSIGN:
	case EXPRESSION_TYPE_MUL_ASSIGN:
	case EXPRESSION_TYPE_DIV_ASSIGN:
	case EXPRESSION_TYPE_MOD_ASSIGN:
		return eval_self_op_assign_expression(inter, env, e);
	case EXPRESSION_TYPE_NE:
	case EXPRESSION_TYPE_EQ:
	case EXPRESSION_TYPE_GE:
	case EXPRESSION_TYPE_GT:
	case EXPRESSION_TYPE_LE:
	case EXPRESSION_TYPE_LT:
		return eval_relation_expression(inter, env, e);
	case EXPRESSION_TYPE_ADD:
	case EXPRESSION_TYPE_SUB:
	case EXPRESSION_TYPE_MUL:
	case EXPRESSION_TYPE_DIV:
	case EXPRESSION_TYPE_MOD:
		return eval_arithmetic_expression(inter, env, e);
	case EXPRESSION_TYPE_STRING:
		return eval_string_expression(inter, e);
	case EXPRESSION_TYPE_LOGICAL_OR:
	case EXPRESSION_TYPE_LOGICAL_AND:
		return eval_logical_expression(inter, env, e);
	case EXPRESSION_TYPE_INCREMENT:
	case EXPRESSION_TYPE_DECREMENT:
	case EXPRESSION_TYPE_PRE_DECREMENT:
	case EXPRESSION_TYPE_PRE_INCREMENT:
		return eval_increment_decrement_expression(inter, env, e);
	case EXPRESSION_TYPE_NEGATIVE:
		return eval_negative_expression(inter, env, e);
	case EXPRESSION_TYPE_CREATE_LOCAL_VARIABLE:
		return eval_create_variable_expression(inter, env, e);
	case EXPRESSION_TYPE_INDEX:
		return eval_index_expression(inter, env, e);
	case EXPRESSION_TYPE_ARRAY:
		return eval_array_expression(inter, env, e);
	case EXPRESSION_TYPE_OBJECT:
		return eval_object_expression(inter, env, e);
	case EXPRESSION_TYPE_FUNCTION_CALL:
	case EXPRESSION_TYPE_EXPRESSION_FUNCTION_CALL:
		return eval_function_call_expression(inter, env, e);
	case EXPRESSION_TYPE_IDENTIFIER:
		return eval_identifier_expression(inter, env, e);
	case EXPRESSION_TYPE_NEW:
		return eval_new_expression(inter, env, e);
	case EXPRESSION_TYPE_METHOD_CALL:
		return eval_method_call_expression(inter, env, e);
	case EXPRESSION_TYPE_ASSIGN_FUNCTION:
		return eval_assign_function_expression(inter, env, e);
	case EXPRESSION_TYPE_FUNCTION:
		v.typ = JS_VALUE_TYPE_FUNCTION;
		v.u.func = e->u.func;
		push_stack(&inter->stack, &v);
		return 0;
	case EXPRESSION_TYPE_NOT:
		return eval_not_expression(inter, env, e);
	case EXPRESSION_TYPE_CREATE_FUNCTION:
		return eval_create_function_expression(inter, env, e);
	}

	return 0;
}

int eval_array_method_push(JsInterpreter *inter, ExecuteEnvironment *env, JsValue *array, ExpressionMethodCall *call)
{
	int length = get_expression_list_length(call->args);
	int total_length = length + array->u.array->length;
	if (total_length > array->u.array->alloc)
	{ //resize array
		JsValue *t = MEM_alloc(inter->execute_memory, sizeof(JsValue) * total_length, call->e->line);
		int i = 0;
		for (; i < array->u.array->length; i++)
		{ //copy from old
			t[i] = array->u.array->elements[i];
		}
		MEM_free(inter->execute_memory, array->u.array->elements);
		array->u.array->elements = t;
	}
	ArgumentList *list = call->args;
	JsValue v;
	JsArray *arr = array->u.array;
	while (NULL != list)
	{
		eval_expression(inter, env, list->expression);
		v = pop_stack(&inter->stack);
		arr->elements[arr->length] = v;
		arr->length++;
		list = list->next;
	}
	v.typ = JS_VALUE_TYPE_INT;
	v.u.intvalue = arr->length;
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_array_method_pop(JsInterpreter *inter, JsValue *array)
{
	JsArray *arr = array->u.array;
	if (arr->length <= 0)
	{
		JsValue v;
		v.typ = JS_VALUE_TYPE_NULL;
		push_stack(&inter->stack, &v);
		return 0;
	}
	arr->length--;
	JsValue v = arr->elements[arr->length];
	push_stack(&inter->stack, &v);
	return 0;
}

int eval_array_method(JsInterpreter *inter, ExecuteEnvironment *env, JsValue *array, ExpressionMethodCall *call)
{
	if (0 == strcmp(call->method, "push"))
	{
		return eval_array_method_push(inter, env, array, call);
	}
	if (0 == strcmp(call->method, "pop"))
	{
		return eval_array_method_pop(inter, array);
	}
	ERROR_runtime_error(RUNTIME_ERROR_METHOD_NOT_FOUND, call->method, call->e->line);
	return RUNTIME_ERROR_METHOD_NOT_FOUND;
}

int eval_method_call_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	ExpressionMethodCall *call = e->u.method_call;

	eval_expression(inter, env, call->e);

	JsValue object = pop_stack(&inter->stack);

	/*handle array*/
	if (JS_VALUE_TYPE_ARRAY == object.typ)
	{
		return eval_array_method(inter, env, &object, call);
	}
	if (JS_VALUE_TYPE_OBJECT != object.typ)
	{
		ERROR_runtime_error(RUNTIME_ERROR_IS_NOT_AN_OBJECT, "", e->line);
		return RUNTIME_ERROR_IS_NOT_AN_OBJECT;
	}

	JsValue *value = INTERPRETER_search_field_from_object_include_prototype(object.u.object, call->method);
	if (NULL == value)
	{
		ERROR_runtime_error(RUNTIME_ERROR_FIELD_NOT_DEFINED, call->method, e->line);
		return RUNTIME_ERROR_FIELD_NOT_DEFINED;
	}
	if (JS_VALUE_TYPE_FUNCTION != value->typ)
	{
		ERROR_runtime_error(RUNTIME_ERROR_NOT_A_FUNCTION, call->method, e->line);
		return RUNTIME_ERROR_NOT_A_FUNCTION;
	}
	JsFunction *func = value->u.func;
	if (JS_FUNCTION_TYPE_BUILDIN == func->typ)
	{
		/*execute buildin function*/
		return eval_build_in_function(inter, env, func->buildin, call->args);
	}

	/*call user function*/

	return eval_method_and_function_call(inter, env, object.u.object, func, call->args, e->line);
}

int eval_build_in_function(JsInterpreter *inter, ExecuteEnvironment *env, JsFunctionBuildin *func, ArgumentList *args)
{
	JsValue vs[BUILD_IN_FUNCTION_MAX_ARGS];
	int i = 0;
	ArgumentList *list = args;
	JsValue value;
	while (NULL != list)
	{
		eval_expression(inter, env, list->expression);
		value = pop_stack(&inter->stack);
		if (i < BUILD_IN_FUNCTION_MAX_ARGS)
		{
			vs[i] = value;
			i++;
		}
		list = list->next;
	}
	JsValue v;
	v.typ = JS_VALUE_TYPE_NULL;
	switch (func->args_count)
	{
	case 1:
		v = func->u.func1(&vs[0]);
		break;
	}

	push_stack(&inter->stack, &v);
	return 0;
}

int eval_identifier_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsValue *v = INTERPRETE_search_variable_from_env(env, e->u.identifier);
	if (NULL == v)
	{
		JsFunction *func = INTERPRETER_search_func_from_env(env, e->u.identifier);
		if (NULL == func)
		{
			ERROR_runtime_error(RUNTIME_ERROR_VARIABLE_NOT_FOUND, e->u.identifier, e->line);
			return RUNTIME_ERROR_VARIABLE_NOT_FOUND;
		}
		else
		{
			JsValue vv;
			vv.typ = JS_VALUE_TYPE_FUNCTION;
			vv.u.func = func;
			v = &vv;
		}
	}
	push_stack(&inter->stack, v);
	return 0;
}

JsValue *get_left_value_from_current_env(ExecuteEnvironment *env, char *name)
{
	int length = strlen(name);
	VariableList *list = env->vars;
	while (NULL != list)
	{
		if (0 == strncmp(list->var.name, name, length))
		{
			return &list->var.value;
		}
		list = list->next;
	}
	return NULL;
}

int eval_create_variable_expression(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	JsValue *dest = NULL;
	VariableList *list = env->vars;
	while (NULL != list)
	{
		if (0 == strcmp(list->var.name, e->u.create_var->identifier))
		{
			dest = &list->var.value;
		}
		list = list->next;
	}
	eval_expression(inter, env, e->u.create_var->expression);
	JsValue value = pop_stack(&inter->stack);
	if (NULL == dest)
	{
		Variable *var = INTERPRETER_create_variable(inter, env, e->u.create_var->identifier, NULL, e->line);
		dest = &var->value;
	}
	if (JS_VALUE_TYPE_STRING_LITERAL == value.typ)
	{
		int length = strlen(value.u.literal_string);
		JsValue newv;
		newv.typ = JS_VALUE_TYPE_STRING;
		newv.u.string = INTERPRETER_create_heap(inter, JS_VALUE_TYPE_STRING, length + 1, e->line);
		strncpy(newv.u.string->s, value.u.literal_string, length);
		newv.u.string->s[length] = 0;
		newv.u.string->length = length;
		*dest = newv;
	}
	else
	{
		*dest = value;
	}
	push_stack(&inter->stack, dest);
	return 0;
}

JsValue *get_left_value_index(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	ExpressionIndex *index = e->u.index;
	eval_expression(inter, env, index->e);
	JsValue v = pop_stack(&inter->stack);
	if (JS_VALUE_TYPE_ARRAY == v.typ)
	{
		if (INDEX_TYPE_IDENTIFIER == index->typ)
		{
			ERROR_runtime_error(RUNTIME_ERROR_INDEX_HAS_WRONG_TYPE, "", e->line);
			return NULL;
		}
		JsArray *array = v.u.array;
		eval_expression(inter, env, index->index);
		JsValue key = pop_stack(&inter->stack);
		if (JS_VALUE_TYPE_INT != key.typ)
		{
			ERROR_runtime_error(RUNTIME_ERROR_INDEX_HAS_WRONG_TYPE, "", e->line);
			return NULL;
		}
		if (key.u.intvalue < 0 || key.u.intvalue >= array->length)
		{
			ERROR_runtime_error(RUNTIME_ERROR_INDEX_OUT_RANGE, "", e->line);
			return NULL;
		}
		return array->elements + key.u.intvalue;
	}
	if (JS_VALUE_TYPE_OBJECT == v.typ)
	{
		char *fieldname = NULL;
		JsValue *dest = NULL;
		if (INDEX_TYPE_IDENTIFIER == index->typ)
		{
			fieldname = index->identifier;
		}
		else
		{
			eval_expression(inter, env, index->index);
			JsValue key = pop_stack(&inter->stack);
			if (JS_VALUE_TYPE_STRING_LITERAL == key.typ)
			{
				fieldname = key.u.literal_string;
			}
			else if (JS_VALUE_TYPE_STRING == key.typ)
			{
				fieldname = key.u.string->s;
			}
		}
		if (NULL == fieldname)
		{
			ERROR_runtime_error(RUNTIME_ERROR_INDEX_HAS_WRONG_TYPE, "", e->line);
			return NULL;
		}
		dest = INTERPRETE_search_field_from_object(v.u.object, fieldname);
		if (NULL == dest)
		{
			dest = INTERPRETE_create_object_field(inter, v.u.object, fieldname, NULL, e->line);
		}
		return dest;
	}

	ERROR_runtime_error(RUNTIME_ERROR_CANNOT_INDEX_THIS_TYPE, "", e->line);
	return NULL;
}

JsValue *get_left_value(JsInterpreter *inter, ExecuteEnvironment *env, Expression *e)
{
	if (EXPRESSION_TYPE_IDENTIFIER == e->typ)
	{
		Variable *var = NULL;
		while (NULL != env)
		{
			var = search_variable_from_variablelist(env->vars, e->u.identifier);
			if (NULL != var)
			{
				return &var->value;
			}
			env = env->outter;
		}
		/*no return,means variable not found,create in inter->env*/
		var = INTERPRETER_create_variable(inter, &inter->env, e->u.identifier, NULL, e->line);
		return &var->value;
	}

	if (EXPRESSION_TYPE_INDEX == e->typ)
	{
		return get_left_value_index(inter, env, e);
	}

	ERROR_runtime_error(RUNTIME_ERROR_CAN_NOT_USE_THIS_AS_LEFT_VALUE, "", e->line);

	return NULL;
}

Variable *
search_variable_from_variablelist(VariableList *list, char *identifier)
{
	if (NULL == list)
	{
		return NULL;
	}
	while (NULL != list)
	{
		if (0 == strcmp(identifier, list->var.name))
		{
			return &list->var;
		}
		list = list->next;
	}
	return NULL;
}
