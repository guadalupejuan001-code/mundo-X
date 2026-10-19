"""Exact additional device contracts required by the Infinite integration."""

GRAPHICS_CASES = {"native_infinite_contracts", "infinite_live_renderer_and_portability"}
COSMIC_CASES = {"infinite_ui_shared_state_and_persistence"}


def verify_graphics_metrics(metrics, require):
    native = metrics["infiniteNativeContracts"]
    require(native["addressDigits"] == 101 and all(native[name] is True for name in (
        "readOnlyQueries", "transactionRollbackIncludesRng", "snapshotContinuationExact",
        "compactionReleasedBackground", "gccFixtureContinuationExact")),
        "Incomplete native Infinite contracts or GCC-to-Android continuation")
    renderer = metrics["infiniteRenderer"]
    address = "1" + "0" * 100
    require(renderer["address"] == address and renderer["partner"] == address[:-1] + "1",
            "Infinite renderer lost its full decimal addresses")
    require(renderer["coupledChangedPixels"] > 20 and renderer["uncoupledChangedPixels"] == 0
            and renderer["roundtripChangedPixels"] == 0 and all(renderer[name] is True for name in (
                "sharedOwner", "readOnlyDraw", "programCacheInvalidated", "sameRevisionEpochInvalidated",
                "snapshotContinuationExact")), "Infinite state, causal pixels or persistence failed")
    return {"native": native, "renderer": renderer}


def exports(report, require):
    values = report.get("infiniteProjectExports", [])
    require(len(values) == 1 and values[0]["file"] == "portable-infinite-android.cosmic"
            and values[0]["stateFile"] == "portable-infinite-android.cosmic.state.json",
            "Missing Infinite current-state Android export")
    return values


def verify_cosmic_metrics(metrics, require):
    value = metrics["infiniteUi"]
    address = "1" + "0" * 100
    require(value["address"] == address and value["partner"] == address[:-1] + "1",
            "Infinite UI lost its full decimal addresses")
    require(value["coupledChangedPixels"] > 20 and value["uncoupledChangedPixels"] == 0
            and value["safRoundtripChangedPixels"] == 0 and value["recreateChangedPixels"] == 0
            and all(value[name] is True for name in ("sharedOwner", "readOnlyAddressSelection",
                "readOnlyDraw", "snapshotContinuationExact", "safUsedProductionCallbacks")),
            "Infinite UI, document callbacks or lifecycle changed the shared state")
    return value
