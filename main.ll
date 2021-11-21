; ModuleID = 'main.ll'
source_filename = "main.ll"

@0 = private unnamed_addr constant [15 x i8] c"Result: %.3lf\0A\00", align 1

define i32 @main() {
entry:
  %0 = fneg double 1.000000e+00
  %1 = fadd double 6.000000e+00, 2.000000e+00
  %2 = fmul double 5.000000e+00, %1
  %3 = fadd double %0, %2
  %4 = fdiv double 1.200000e+01, 4.000000e+00
  %5 = fsub double %3, %4
  %6 = call double @pow(double 2.000000e+00, double 4.000000e+00)
  %7 = fadd double %5, %6
  %8 = fadd double %7, 0x400921FB54442D18
  %9 = fmul double 0x4005BF0A8B145769, 1.010000e-01
  %10 = fsub double %8, %9
  %11 = fptosi double 1.000000e+00 to i32
  %12 = fptosi double 5.000000e+00 to i32
  %13 = shl i32 %11, %12
  %14 = sitofp i32 %13 to double
  %15 = fsub double %10, %14
  %16 = fneg double 2.000000e+00
  %17 = call double @hypot(double 1.000000e+00, double %16)
  %18 = call double @hypot(double %17, double 3.000000e+00)
  %19 = fneg double %18
  %20 = call double @fmin(double 4.000000e+00, double 5.000000e+00)
  %21 = call double @fmax(double 1.000000e+00, double 2.000000e+00)
  %22 = call double @fmax(double %21, double %20)
  %23 = fmul double %19, %22
  %24 = fadd double %15, %23
  %25 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @0, i32 0, i32 0), double %24)
  ret i32 0
}

declare double @pow(double, double)

declare double @hypot(double, double)

declare double @fmin(double, double)

declare double @fmax(double, double)

declare i32 @printf(i8*, ...)
