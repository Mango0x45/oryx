define i64 @foo() {
entry:
  %x = alloca i64, align 8
  store i64 42, ptr %x, align 8
  %load = load i64, ptr %x, align 8
  %add = add i64 %load, 84
  ret i64 %add
}

define i64 @bar() {
entry:
  ret i64 43
}
