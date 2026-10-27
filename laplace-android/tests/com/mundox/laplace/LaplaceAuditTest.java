package com.mundox.laplace;
import java.util.Locale;
public final class LaplaceAuditTest {
 public static void main(String[] args){
    LaplaceAudit.Report r=LaplaceAudit.evaluate();
    if(!r.passed||r.checks<100)throw new AssertionError("not enough independent checks");
    if(r.quadraticGain1<3.5||r.quadraticGain2<3.5||r.quadraticGain3<3.5)throw new AssertionError("order of convergence not demonstrated");
    System.out.printf(Locale.ROOT,"LAPLACE_AUDIT_PASS checks=%d max_discrete_residual=%.4g convergence=[%.4f %.4f %.4f]%n",r.checks,r.maxObservedResidual,r.quadraticGain1,r.quadraticGain2,r.quadraticGain3);
    System.out.print(r.asCsv());
 }
}
