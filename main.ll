; ModuleID = 'main.ll'
source_filename = "main.ll"

@0 = private unnamed_addr constant [15 x i8] c"Result: %.3lf\0A\00", align 1

define i32 @main() {
entry:
  %0 = fadd double 6.000000e+00, 2.000000e+00
  %1 = fmul double 5.000000e+00, %0
  %2 = fdiv double 1.200000e+01, 4.000000e+00
  %3 = fsub double %1, %2
  %4 = call double @pow(double 2.000000e+00, double 4.000000e+00)
  %5 = fadd double %3, %4
  %6 = fadd double %5, 0x400921FB54442D18
  %7 = fmul double 0x4005BF0A8B145769, 1.010000e-01
  %8 = fsub double %6, %7
  %9 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @0, i32 0, i32 0), double %8)
  ret i32 0
}

declare double @pow(double, double)

declare i32 @printf(i8*, ...)
