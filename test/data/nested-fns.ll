define i64 @fn1.fn2.fn1() {
entry:
  ret i64 42
}

define i64 @fn1.fn2() {
entry:
  %call = call i64 @fn1.fn2.fn1()
  %call1 = call i64 @fn1.fn2.fn1()
  %mul = mul i64 %call, %call1
  ret i64 %mul
}

define i64 @fn1() {
entry:
  %call = call i64 @fn1.fn2()
  ret i64 %call
}
