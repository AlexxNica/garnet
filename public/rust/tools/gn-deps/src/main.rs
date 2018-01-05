#![deny(warnings)]

extern crate serde;
extern crate structopt;
extern crate toml;

#[macro_use] extern crate failure;
#[macro_use] extern crate serde_derive;
#[macro_use] extern crate structopt_derive;

use std::collections::BTreeMap;
use std::collections::HashSet;
use std::env;
use std::fs::File;
use std::io::prelude::*;
use std::path::{Path, PathBuf};

use failure::err_msg;
use structopt::StructOpt;
use toml::Value as Toml;

type FResult<T> = Result<T, failure::Error>;

// Cargo structures

#[derive(Debug, Deserialize)]
struct Manifest {
    package: Option<Package>,
    dependencies: Option<Toml>,
    workspace: Option<Workspace>,
    patch: Option<PatchTable>,
}

#[derive(Debug, Deserialize)]
struct Package {
    name: Option<String>,
}

#[derive(Debug, Deserialize)]
struct Workspace {
    members: Option<Vec<String>>,
}

#[derive(Debug, Deserialize)]
struct PatchTable {
    #[serde(rename = "crates-io")]
    crates_io: Option<BTreeMap<String, Patch>>,
}

#[derive(Debug, Deserialize)]
struct Patch {
    path: String,
}

pub fn fuchsia_root(release_os: bool, target_cpu: &str) -> FResult<PathBuf> {
    let fuchsia_root_value = if let Ok(fuchsia_root_value) = env::var("FUCHSIA_ROOT") {
        fuchsia_root_value
    } else {
        let mut path = env::current_dir()?;
        loop {
            if possible_target_out_dir(&path, release_os, target_cpu).is_ok() {
                return Ok(path);
            }
            path = if let Some(path) = path.parent() {
                path.to_path_buf()
            } else {
                bail!(
                    "FUCHSIA_ROOT not set and current directory is not in a Fuchsia tree with a \
                    release-x86-64 build. You must set the environmental variable FUCHSIA_ROOT to \
                    point to a Fuchsia tree with a release-x86-64 build."
                )
            }
        }
    };
    Ok(PathBuf::from(fuchsia_root_value))
}

pub fn possible_target_out_dir(fuchsia_root: &PathBuf, release_os: bool, target_cpu: &str) -> FResult<PathBuf> {
    let out_dir_name_prefix = if release_os { "release" } else { "debug" };
    let out_dir_name = format!("{}-{}", out_dir_name_prefix, target_cpu);
    let target_out_dir = fuchsia_root.join("out").join(out_dir_name);
    if !target_out_dir.exists() {
        bail!("no target out directory found at  {:?}", target_out_dir);
    }
    Ok(target_out_dir)
}

fn get_dependency_names(manifest: &str) -> FResult<HashSet<String>> {
    let decoded: Manifest = toml::from_str(&manifest)?;
    let deps = decoded.dependencies.ok_or(err_msg("Crate manifest had no dependencies."))?;
    let deps_table = if let Toml::Table(table) = deps { table } else {
        bail!("Crate manifest dependencies not a table")
    };

    deps_table.into_iter()
        .map(|(key, value)|
            if let Toml::String(_version) = value {
                Ok(key)
            } else {
                bail!("Crate {} manifest has a non-string dependency", key)
            }
        )
        .collect()
}

fn get_crates_with_build_files(
    workspace: &str,
    workspace_root: &Path,
) -> FResult<BTreeMap<String, PathBuf>> {
    let decoded: Manifest = toml::from_str(&workspace)?;
    let patches = decoded.patch.ok_or(err_msg("Crate manifest had no patch section."))?;
    let crates_patches =
        patches.crates_io.ok_or(err_msg("Crate manifest had no patch section for crates-io."))?;

    let mut crate_paths = BTreeMap::new();
    for (key, value) in crates_patches {
        let crate_path = workspace_root.join(&value.path).canonicalize()?;
        let build_gn_path = crate_path.join("BUILD.gn");
        if build_gn_path.exists() {
            crate_paths.insert(key, crate_path);
        }
    }
    Ok(crate_paths)
}

fn print_gn_deps(opts: Opts) -> FResult<()> {
    let crate_path = PathBuf::from(opts.crate_path);
    let full_path = crate_path.canonicalize()?;
    let cargo_toml_path = full_path.join("Cargo.toml");

    let mut toml_str = String::new();
    File::open(cargo_toml_path)?.read_to_string(&mut toml_str)?;

    let dep_names = get_dependency_names(&toml_str)?;
    let fuchsia_root = fuchsia_root(opts.release_os, &opts.target_cpu)?;
    let garnet_root = fuchsia_root.join("garnet");
    let workspace_path = garnet_root.join("Cargo.toml");

    let mut workspace_contents_str = String::new();
    File::open(workspace_path)?.read_to_string(&mut workspace_contents_str)?;

    let crates_with_build_files =
        get_crates_with_build_files(&workspace_contents_str, &garnet_root)?;

    for crate_name in dep_names {
        match crates_with_build_files.get(&crate_name) {
            Some(path) => println!("{}", path.to_string_lossy()),
            None => (),
        }
    }
    Ok(())
}

#[derive(StructOpt, Debug)]
pub struct Opts {
    #[structopt(long = "release", default_value = "true")]
    pub release_os: bool,

    #[structopt(long = "target-cpu", default_value = "x86-64")]
    pub target_cpu: String,

    #[structopt(long = "device-name", default_value = "None")]
    pub device_name: Option<String>,

    pub crate_path: String,
}

fn main() {
    let opts = Opts::from_args();
    if let Err(e) = print_gn_deps(opts) {
        eprintln!("Error: {}", e);
        ::std::process::exit(1);
    }
}
