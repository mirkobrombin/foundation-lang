#[test]
fn calls_foundation_archive() {
    assert_eq!(sample_native::foundation_increment(41), 42);
    assert_eq!(sample_native::foundation_invoke(increment, 20), 21);
}

unsafe extern "C" fn increment(value: i32) -> i32 {
    value + 1
}
