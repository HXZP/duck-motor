# 项目指令

在本项目中生成、修改或重构代码时，必须始终遵守以下规则：

1. 所有函数都必须添加 Doxygen 风格注释。
2. 函数注释必须包含以下内容：
   - `@brief`
   - 每个参数对应的 `@param`
   - 函数有返回值时必须包含 `@return`
   - 需要补充说明时添加 `@note`
3. 代码必须使用非紧凑型格式，禁止紧凑写法。
4. `if`、`else`、`for`、`while`、`switch` 以及对应花括号必须分别独立成行。
5. 禁止使用 `} else {` 这类紧凑写法。
6. 所有新增代码和所有修改后的代码都必须符合以上规则。
7. 使用UTF-8编码，请使用中文回复和注释
8. 修改代码前需要请求同意
9. 不要在函数前添加(void)，例如(void)Load_Recoder(&data);
示例：

```c
/**
 * @brief 初始化 FOC 控制器。
 * @param foc FOC 控制器句柄指针。
 * @return void
 */
void Foc_Init(FocHandle_t *foc)
{
    if (foc == NULL)
    {
        return;
    }
    else
    {
        foc->state = FOC_STATE_IDLE;
    }
}
```
