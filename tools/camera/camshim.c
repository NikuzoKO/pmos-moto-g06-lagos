/*
 * LD_PRELOAD for the stock cameraserver without system_server: framework
 * services it would block on forever are reported as absent instead.
 */

/* sp<IActivityManager> android::ActivityManager::getService():
 * the sp<> is returned through the x8 result pointer; store nullptr there.
 * Every caller checks for null (UidPolicy, SensorPrivacy, ...). */
__asm__(
	".globl _ZN7android15ActivityManager10getServiceEv\n"
	".type _ZN7android15ActivityManager10getServiceEv, %function\n"
	"_ZN7android15ActivityManager10getServiceEv:\n"
	"\tstr xzr, [x8]\n"
	"\tret\n");

/* No permission_checker / appops services: every PermissionChecker check is
 * PERMISSION_GRANTED (0), every app op MODE_ALLOWED (0), finishOp a no-op. */
#define RETURN_ZERO(symbol) __asm__(".globl " symbol "\n.type " symbol ", %function\n" \
	symbol ":\n\tmov w0, #0\n\tret\n")
RETURN_ZERO("_ZN7android10permission17PermissionChecker15checkPermissionERKNS_8String16ERKNS_7content22AttributionSourceStateES4_bbbi");
RETURN_ZERO("_ZN7android10permission17PermissionChecker27checkPermissionForPreflightERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i");
RETURN_ZERO("_ZN7android10permission17PermissionChecker41checkPermissionForPreflightFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i");
RETURN_ZERO("_ZN7android10permission17PermissionChecker44checkPermissionForDataDeliveryFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i");
RETURN_ZERO("_ZN7android10permission17PermissionChecker49checkPermissionForStartDataDeliveryFromDatasourceERKNS_8String16ERKNS_7content22AttributionSourceStateES4_i");
RETURN_ZERO("_ZN7android13AppOpsManager14startOpNoThrowEiiRKNS_8String16Eb");
RETURN_ZERO("_ZN7android13AppOpsManager14startOpNoThrowEiiRKNS_8String16EbRKNSt3__18optionalIS1_EES3_");
RETURN_ZERO("_ZN7android13AppOpsManager6noteOpEiiRKNS_8String16E");
RETURN_ZERO("_ZN7android13AppOpsManager6noteOpEiiRKNS_8String16ERKNSt3__18optionalIS1_EES3_");
RETURN_ZERO("_ZN7android13AppOpsManager7checkOpEiiRKNS_8String16E");
RETURN_ZERO("_ZN7android13AppOpsManager8finishOpEiiRKNS_8String16E");
RETURN_ZERO("_ZN7android13AppOpsManager8finishOpEiiRKNS_8String16ERKNSt3__18optionalIS1_EE");
