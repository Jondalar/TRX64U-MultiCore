# shellcheck shell=bash
# The host application built natively (T236 D12): the sources, defines and include path of tests/hostapp/render/run.sh and tools/menusim/run.sh --host.
# Sourced with ROOT set. hostapp_srcs fills HOST_SRCS with every source of the build but the four a negative control of the bench replaces (hostui.cc,
# discover.cc, cfw.cc, cfwicon.cc) and the program itself; HC_SRC, HCF_SRC, HCR_SRC, HCW_SRC and HCU_SRC, when set, replace their file.
H="$ROOT/host-app/software/hostapp"
ULT="${ULTIMATE_SW:-$ROOT/vendor/gideon/software}"   # ULTIMATE_SW points at another copy
L="$ULT/lvgl"
CORE="$ULT"
HOST_DEFS="-std=gnu++14 -w -DHOSTUI_RENDER_TEST -DVIDEOMODE_NO_BOARD=1 -DWEBUI_NO_BOARD=1 -DLV_CONF_INCLUDE_SIMPLE"
# after the directory of lv_conf.h and the bench's stub directory, which the caller puts first
HOST_INCS="-I$H -iquote $L -I$ROOT/app/view -I$ROOT/uicore -I$ROOT/coreapp/src -I$ROOT/app/edit -I$ROOT/tests -I$ROOT/app/video -I$CORE/io/c64 -I$CORE/components -I$CORE/system -I$ROOT/host-app/software/filesystem -I$CORE/io/flash"

hostapp_srcs() {
  HOST_SRCS="$ROOT/app/view/trxview.cc $ROOT/app/video/videomode.cc $ROOT/app/video/edid.cc $ROOT/coreapp/src/picmodes.cc $H/hostvideo.cc ${HC_SRC:-$H/host_core.cc} ${HCF_SRC:-$H/host_core_flow.cc} ${HCR_SRC:-$H/host_core_flow_restore.cc} $H/host_core_flow_convert.cc $H/host_core_flow_esp.cc $H/host_core_flow_fileops.cc $H/host_core_flow_wifi.cc $H/host_core_flow_format.cc $H/host_core_nav.cc $H/host_core_cm.cc $H/host_core_cfgov.cc $H/configlist.cc ${HCW_SRC:-$H/host_core_rows.cc} ${HCU_SRC:-$H/host_core_ui.cc} $H/host_core_model.cc $H/hostdecl.cc $H/hostdomain.cc $H/startfile.cc $ROOT/uicore/uic_json.cc $ROOT/uicore/uic_sha.cc $ROOT/uicore/uic_svc.cc $ROOT/uicore/uic_net.cc $H/webui.cc $H/hostreport.cc $H/hostreport_live.cc $H/bugreport.cc $H/cor.cc $H/directboot.cc $H/bootcfg.cc $H/menusnap.cc $H/menurows.cc $H/fileops.cc $H/coremaint.cc $ROOT/app/edit/lineedit.cc $H/startcheck.cc $H/handover.cc $H/sha256.c $H/fmtconfirm.cc $H/mediaui.cc $H/mediasniff.cc $H/fwknown.cc $H/fwconvert.cc $H/fwrestore.cc $H/flashfw.cc $H/fwconfirm.cc $H/txu.cc $H/espinspect.cc $H/espflash.cc $H/host_core_flow_zip.cc $ROOT/coreapp/src/appindex.cc $ROOT/coreapp/src/libload.cc $ROOT/coreapp/src/libscan.cc $ROOT/coreapp/src/libmenu.cc $ROOT/coreapp/src/descrec.cc $ROOT/coreapp/src/ops.cc $ROOT/coreapp/src/corefolder.cc"
}
