import com.mundox.graphics.WearLinkRules;

/** Run with plain javac; tests the exact gesture code used in COSMIC X. */
public final class WearLinkRulesCheck {
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        WearLinkRules r = new WearLinkRules();
        check(!r.observe(100,0,0,9.8f,0,0,0,true),"initial sample cannot trigger");
        check(!r.observe(400,0,0,9.8f,5,0,0,true),"gyro alone must not trigger");
        check(r.observe(700,3,0,9.8f,5,0,0,true),"deliberate twist should trigger");
        check(!r.observe(900,6,0,9.8f,5,0,0,true),"cooldown rejects repeats");
        check(r.observe(2600,9,0,9.8f,5,0,0,true),"new twist allowed after cooldown");
        r.reset();
        check(!r.observe(100,0,0,9.8f,0,0,0,false),"reset sample is inert");
        check(!r.observe(300,6,0,9.8f,5,0,0,false),"unarmed never triggers");
        check(!r.observe(700,Float.NaN,0,0,5,0,0,true),"NaN rejected");
        System.out.println("World Link gesture tests: OK");
    }
}
