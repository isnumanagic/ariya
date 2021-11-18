; ModuleID = 'main.ll'
source_filename = "main.ll"

@0 = private unnamed_addr constant [15 x i8] c"Result: %.3lf\0A\00", align 1

define i32 @main() {
entry:
  %0 = fmul double 2.000000e+00, 4.000000e+00
  %1 = fneg double %0
  %2 = call double @pow(double 3.000000e+00, double 4.000000e+00)
  %3 = fdiv double %2, 2.000000e+01
  %4 = fadd double %1, %3
  %5 = fptosi double 1.000000e+00 to i32
  %6 = fptosi double 4.000000e+00 to i32
  %7 = shl i32 %5, %6
  %8 = sitofp i32 %7 to double
  %9 = fsub double %4, %8
  %10 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @0, i32 0, i32 0), double %9)
  ret i32 0
}

declare double @pow(double, double)

declare i32 @printf(i8*, ...)
