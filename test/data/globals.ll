@x = internal global i64 0
@y = internal global i16 0
@"x\E2\80\B2" = internal global i64 42
@"y\E2\80\B2" = internal global i64 69
@"x\E2\80\B3" = internal global i64 42
@"y\E2\80\B3" = internal global i16 69

define i64 @foo() {
entry:
  %x = alloca i64, align 8
  %load = load i64, ptr @x, align 8
  %add = add i64 %load, 1
  store i64 %add, ptr %x, align 8
  %load1 = load i64, ptr %x, align 8
  ret i64 %load1
}
