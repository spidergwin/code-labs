struct Package {
    id: u32,
}

fn ship_package(p: Package) {
    println!("The package to be shipped has id: {}", p.id)
}

fn main() {
    let my_pkg = Package { id: 101 };

    ship_package(my_pkg);
}
