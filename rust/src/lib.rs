#![allow(unused)]

use std::{cell::LazyCell, collections::HashMap, ffi::{c_char, CStr, CString}, ptr::{addr_of, addr_of_mut}};

use serde::Deserialize;

#[derive(Debug, Clone, Deserialize)]
struct StatsOrder {
    datetime: String,
    volume: f32,
    min_price: f32,
    max_price: f32,
    avg_price: f32,
    median: f32,
    id: String,
}

#[derive(Debug, Clone, Deserialize)]
struct StatsThing {
    #[serde(rename = "48hours")]
    hours_48: Vec<StatsOrder>,
    #[serde(rename = "90days")]
    days_90: Vec<StatsOrder>
}

#[derive(Debug, Clone, Deserialize)]
struct StatsPayload {
    statistics_closed: StatsThing,
    statistics_live: StatsThing,
}

#[derive(Debug, Clone, Deserialize)]
struct Stats {
    payload: StatsPayload
}

#[derive(Debug, Clone, Deserialize)]
struct Item {
    item_name: String,
    url_name: String,
}

#[derive(Debug, Clone, Deserialize)]
struct ItemsThing {
    items: Vec<Item>
}

#[derive(Debug, Clone, Deserialize)]
struct Items {
    payload: ItemsThing
}

#[derive(Debug, Clone, Deserialize)]
struct SetThing {
    include: SetThing2
}

#[derive(Debug, Clone, Deserialize)]
struct SetThing2 {
    item: SetThingItem
}

#[derive(Debug, Clone, Deserialize)]
struct SetThingItem {
    items_in_set: Vec<SetThingItemItem>
}

#[derive(Debug, Clone, Deserialize)]
struct SetThingItemItem {
    url_name: String
}

/// (Name, UrlName)
static mut ITEMS: Vec<(String, String)> = Vec::new();
/// Tesseract output -> (min, min/url str)
static mut DIST_CACHE: Option<HashMap<String, (usize, String)>> = None;
/// url_name -> average price
static mut AVG_CACHE: Option<HashMap<String, f32>> = None;
/// url_name -> url_name
static mut SET_CACHE: Option<HashMap<String, String>> = None;

#[inline(always)]
fn tail(a: &str) -> &str {
    a.split_at(1).1
}

// ChatGPT go brr bcs mine is way too slow
fn lev(a: &str, b: &str) -> usize {
    // if b.len() == 0 {
    //     return a.len();
    // }

    // if a.len() == 0 {
    //     return b.len();
    // }

    // let tail_a = tail(a);
    // let tail_b = tail(b);

    // if a.chars().nth(0).unwrap() == b.chars().nth(0).unwrap() {
    //     return lev(tail_a, tail_b)
    // }
    
    // let first = lev(tail_a, b);
    // let second = lev(a, tail_b);
    // let third = lev(tail_a, tail_b);

    // return 1 + first.min(second).min(third);

    let m = a.len();
    let n = b.len();
    let mut dp = vec![vec![0; n + 1]; m + 1];

    for i in 0..=m {
        for j in 0..=n {
            if i == 0 {
                dp[i][j] = j;
            } else if j == 0 {
                dp[i][j] = i;
            } else if a.chars().nth(i - 1) == b.chars().nth(j - 1) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + dp[i - 1][j].min(dp[i][j - 1]).min(dp[i - 1][j - 1]);
            }
        }
    }

    dp[m][n]
}

unsafe fn get_min_str(item: String) -> (usize, String) {
    if DIST_CACHE == None { DIST_CACHE = Some(HashMap::new()); }

    let dist_cache = (&mut *addr_of_mut!(DIST_CACHE)).as_mut().unwrap();
    if let Some(thing) = dist_cache.get(&item) {
        return thing.clone();
    }

    let mut min = usize::MAX;
    let mut min_str = "".to_string();
    for thing in &*addr_of!(ITEMS) {
        let dist = lev(item.as_str(), thing.0.as_str());
        if dist < min {
            min = dist;
            min_str = thing.clone().1;
        }
    }

    dist_cache.insert(item, (min, min_str.clone()));

    (min, min_str)
}

fn fetch_and_cache(url: String, cache_name: String) -> String {
    let path = format!("cache/{}.json", cache_name);
    std::fs::create_dir_all("cache").unwrap();
    if std::fs::exists(path.clone()).unwrap() {
        std::fs::read_to_string(path).unwrap()
    } else {
        println!("{} not in cache, downloading...", cache_name);
        let text = reqwest::blocking::get(url)
            .unwrap()
            .text()
            .unwrap();
        std::fs::write(path, text.clone());
        text
    }
}

unsafe fn get_item_stuff(item: String, thing: u8) -> (f32, f32) {
    let text = fetch_and_cache(format!("https://api.warframe.market/v1/items/{}/statistics", item), item.clone());

    let stats = serde_json::from_str::<Stats>(&text).unwrap();

    let mut stuff: Vec<&StatsOrder> = vec![];
    if thing & 0b0001 != 0 {
        stuff.extend(&stats.payload.statistics_closed.hours_48);
    }
    if thing & 0b0010 != 0 {
        stuff.extend(&stats.payload.statistics_live.hours_48);
    }
    if thing & 0b0100 != 0 {
        stuff.extend(&stats.payload.statistics_closed.days_90);
    }
    if thing & 0b1000 != 0 {
        stuff.extend(&stats.payload.statistics_live.days_90);
    }

    let mut results = (0.0, 0.0);
    for order in stuff {
        results = (results.0 + order.avg_price, results.1 + 1.0);
    }

    (results.0, results.1)
}

#[no_mangle]
pub unsafe extern "C" fn init_thingy() {
    let text = fetch_and_cache("https://api.warframe.market/v1/items".to_string(), "items".to_string());

    let json = serde_json::from_str::<Items>(&text).unwrap();
    for item in json.payload.items {
        let name = item.item_name.to_lowercase();
        if name.contains("prime") && !name.starts_with("primed") && !name.ends_with("set") && name != "gotva prime" {
            ITEMS.push((item.item_name, item.url_name));
        }
    }
    // Will make get_min_str of forma return forma_blueprint which we can check to ignore forma
    // I could do lev of it every time we try to fetch an item but that wouldnt be cached and i want to do this
    ITEMS.push(("Forma Blueprint".to_string(), "forma_blueprint".to_string()));
}

#[no_mangle]
pub unsafe extern "C" fn get_item(item: *const c_char) -> *mut c_char {
    let item = CStr::from_ptr(item).to_str().unwrap().to_string().to_lowercase().replace('\n', " ");
    
    let (min, min_str) = get_min_str(item);
    if min > 8 {
        return std::ptr::null_mut();
    }

    return CString::new(min_str).unwrap().into_raw();
}

#[no_mangle]
pub unsafe extern "C" fn free_rstring(item: *mut c_char) {
    CString::from_raw(item);
}

#[no_mangle]
pub unsafe extern "C" fn get_set_price(item: *mut c_char, thing: u8) -> f32 {
    let item = CStr::from_ptr(item).to_str().unwrap().to_string();
    if item == "forma_blueprint" { return -1.0; }

    if SET_CACHE == None { SET_CACHE = Some(HashMap::new()); }
    if AVG_CACHE == None { AVG_CACHE = Some(HashMap::new()); }

    let set_cache = (&mut *addr_of_mut!(SET_CACHE)).as_mut().unwrap();
    let avg_cache = (&mut *addr_of_mut!(AVG_CACHE)).as_mut().unwrap();
    let mut set = "".to_string();
    if let Some(thing) = set_cache.get(&item) {
        if let Some(avg) = avg_cache.get(thing) {
            return *avg;
        }
        set = thing.clone();
    } else {
        let text = fetch_and_cache(format!("https://api.warframe.market/v1/items/{}/dropsources?include=item", item), format!("{}_dropsources", item));
        let setthing = serde_json::from_str::<SetThing>(&text).unwrap();
        for thing in &setthing.include.item.items_in_set {
            if thing.url_name.ends_with("set") {
                set = thing.url_name.clone();
                set_cache.insert(item.clone(), thing.url_name.clone());
            }
        }
    }
    // Maybe some item doesnt have a set so it wouldnt be put into the cache and would crash elsewhere or keep trying to get the set
    assert!(set != "", "{} does not have set", item);

    let results = get_item_stuff(set.clone(), thing);
    avg_cache.insert(set, results.0 / results.1);
    (results.0 / results.1)
}

#[no_mangle]
pub unsafe extern "C" fn get_item_price(item: *mut c_char, thing: u8) -> f32 {
    let item = CStr::from_ptr(item).to_str().unwrap().to_string();
    if item == "forma_blueprint" { return -1.0; }

    if AVG_CACHE == None { AVG_CACHE = Some(HashMap::new()); }
    let avg_cache = (&mut *addr_of_mut!(AVG_CACHE)).as_mut().unwrap();

    if let Some(avg) = avg_cache.get(&item) {
        return *avg;
    }

    let results = get_item_stuff(item.clone(), thing);
    avg_cache.insert(item, results.0 / results.1);
    results.0 / results.1
}
