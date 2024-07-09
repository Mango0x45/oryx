@x = internal global i64 0
@foo.bar = internal global i64 42
@foo.baz = internal global float 0.000000e+00
@bar.x = internal global i128 1000
@baz.nested.x = internal global i16 0
@baz.x = internal global i128 1000
@baz.y = internal global i64 42

define void @foo() {
entry:
  %load = load i64, ptr @foo.bar, align 8
  %mul = mul i64 %load, 2
  store i64 %mul, ptr @foo.bar, align 8
  ret void
}

define i128 @bar() {
entry:
  %load = load i128, ptr @bar.x, align 16
  ret i128 %load
}

define void @baz.nested() {
entry:
  store i16 16, ptr @baz.nested.x, align 2
  ret void
}

define i128 @baz() {
entry:
  %load = load i128, ptr @baz.x, align 16
  ret i128 %load
}
