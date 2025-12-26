
(expression_if) @keyword.control
(expression_if
  "else" @keyword.control
)

(expression_while) @keyword.control
(expression_for) @keyword.control
(expression_return) @keyword.control
(expression_continue) @keyword.control
(expression_break) @keyword.control
(expression_var_declare) @keyword.declaration

(constant) @constant
(number) @number
(string) @string

(identifier) @variable
(key) @variable.special

(expression_call
  function: (identifier) @function.call
)

(expression_modify) @operator.assignment
(expression_binary) @operator.arithmetic
(expression_unary) @operator.arithmetic
(expression_select) @operator.ternary

(block_explicit) @punctuation.bracket
(expression_paren) @punctuation.bracket
(expression_for_config) @punctuation.bracket
(argument_list_paren) @punctuation.bracket

(comment) @comment
(separator) @punctuation.separator
