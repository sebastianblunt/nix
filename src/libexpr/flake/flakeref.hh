#pragma once

#include "types.hh"
#include "hash.hh"

#include <variant>

namespace nix {

/* Flake references are a URI-like syntax to specify a flake.

   Examples:

   * <flake-id>(/rev-or-ref(/rev)?)?

     Look up a flake by ID in the flake lock file or in the flake
     registry. These must specify an actual location for the flake
     using the formats listed below. Note that in pure evaluation
     mode, the flake registry is empty.

     Optionally, the rev or ref from the dereferenced flake can be
     overriden. For example,

       nixpkgs/19.09

     uses the "19.09" branch of the nixpkgs' flake GitHub repository,
     while

       nixpkgs/98a2a5b5370c1e2092d09cb38b9dcff6d98a109f

     uses the specified revision. For Git (rather than GitHub)
     repositories, both the rev and ref must be given, e.g.

       nixpkgs/19.09/98a2a5b5370c1e2092d09cb38b9dcff6d98a109f

   * github:<owner>/<repo>(/<rev-or-ref>)?

     A repository on GitHub. These differ from Git references in that
     they're downloaded in a efficient way (via the tarball mechanism)
     and that they support downloading a specific revision without
     specifying a branch. <rev-or-ref> is either a commit hash ("rev")
     or a branch or tag name ("ref"). The default is: "master" if none
     is specified. Note that in pure evaluation mode, a commit hash
     must be used.

     Flakes fetched in this manner expose "rev" and "lastModified"
     attributes, but not "revCount".

     Examples:

       github:edolstra/dwarffs
       github:edolstra/dwarffs/unstable
       github:edolstra/dwarffs/41c0c1bf292ea3ac3858ff393b49ca1123dbd553

   * git+https://<server>/<path>(\?attr(&attr)*)?
     git+ssh://<server>/<path>(\?attr(&attr)*)?
     git://<server>/<path>(\?attr(&attr)*)?
     file:///<path>(\?attr(&attr)*)?

     where 'attr' is one of:
       rev=<rev>
       ref=<ref>

     A Git repository fetched through https. The default for "ref" is
     "master".

     Examples:

       git+https://example.org/my/repo.git
       git+https://example.org/my/repo.git?ref=release-1.2.3
       git+https://example.org/my/repo.git?rev=e72daba8250068216d79d2aeef40d4d95aff6666
       git://github.com/edolstra/dwarffs.git?ref=flake&rev=2efca4bc9da70fb001b26c3dc858c6397d3c4817

   * /path(\?attr(&attr)*)?

     Like file://path, but if no "ref" or "rev" is specified, the
     (possibly dirty) working tree will be used. Using a working tree
     is not allowed in pure evaluation mode.

     Examples:

       /path/to/my/repo
       /path/to/my/repo?ref=develop
       /path/to/my/repo?rev=e72daba8250068216d79d2aeef40d4d95aff6666

   * https://<server>/<path>.tar.xz(?hash=<sri-hash>)
     file:///<path>.tar.xz(?hash=<sri-hash>)

     A flake distributed as a tarball. In pure evaluation mode, an SRI
     hash is mandatory. It exposes a "lastModified" attribute, being
     the newest file inside the tarball.

     Example:

       https://releases.nixos.org/nixos/unstable/nixos-19.03pre167858.f2a1a4e93be/nixexprs.tar.xz
       https://releases.nixos.org/nixos/unstable/nixos-19.03pre167858.f2a1a4e93be/nixexprs.tar.xz?hash=sha256-56bbc099995ea8581ead78f22832fee7dbcb0a0b6319293d8c2d0aef5379397c

  Note: currently, there can be only one flake per Git repository, and
  it must be at top-level. In the future, we may want to add a field
  (e.g. "dir=<dir>") to specify a subdirectory inside the repository.
*/

typedef std::string FlakeId;
typedef std::string FlakeAlias;
typedef std::string FlakeUri;

struct FlakeRef
{
    struct IsAlias
    {
        FlakeAlias alias;
        bool operator<(const IsAlias & b) const { return alias < b.alias; };
        bool operator==(const IsAlias & b) const { return alias == b.alias; };
    };

    struct IsGitHub {
        std::string owner, repo;
        bool operator<(const IsGitHub & b) const {
            return std::make_tuple(owner, repo) < std::make_tuple(b.owner, b.repo);
        }
        bool operator==(const IsGitHub & b) const {
            return owner == b.owner && repo == b.repo;
        }
    };

    // Git, Tarball
    struct IsGit
    {
        std::string uri;
        bool operator<(const IsGit & b) const { return uri < b.uri; }
        bool operator==(const IsGit & b) const { return uri == b.uri; }
    };

    struct IsPath
    {
        Path path;
        bool operator<(const IsPath & b) const { return path < b.path; }
        bool operator==(const IsPath & b) const { return path == b.path; }
    };

    // Git, Tarball

    std::variant<IsAlias, IsGitHub, IsGit, IsPath> data;

    std::optional<std::string> ref;
    std::optional<Hash> rev;
    Path subdir = ""; // This is a relative path pointing at the flake.nix file's directory, relative to the git root.

    bool operator<(const FlakeRef & flakeRef) const
    {
        return std::make_tuple(data, ref, rev, subdir) <
            std::make_tuple(flakeRef.data, flakeRef.ref, flakeRef.rev, subdir);
    }

    bool operator==(const FlakeRef & flakeRef) const
    {
        return std::make_tuple(data, ref, rev, subdir) ==
            std::make_tuple(flakeRef.data, flakeRef.ref, flakeRef.rev, flakeRef.subdir);
    }

    // Parse a flake URI.
    FlakeRef(const std::string & uri, bool allowRelative = false);

    // FIXME: change to operator <<.
    std::string to_string() const;

    /* Check whether this is a "direct" flake reference, that is, not
       a flake ID, which requires a lookup in the flake registry. */
    bool isDirect() const
    {
        return !std::get_if<FlakeRef::IsAlias>(&data);
    }

    /* Check whether this is an "immutable" flake reference, that is,
       one that contains a commit hash or content hash. */
    bool isImmutable() const;

    FlakeRef baseRef() const;

    bool isDirty() const
    {
        return std::get_if<FlakeRef::IsPath>(&data)
            && rev == Hash(rev->type);
    }

    /* Return true if 'other' is not less specific than 'this'. For
       example, 'nixpkgs' contains 'nixpkgs/release-19.03', and both
       'nixpkgs' and 'nixpkgs/release-19.03' contain
       'nixpkgs/release-19.03/<hash>'. */
    bool contains(const FlakeRef & other) const;
};

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef);

MakeError(BadFlakeRef, Error);
MakeError(MissingFlake, BadFlakeRef);

std::optional<FlakeRef> parseFlakeRef(
    const std::string & uri, bool allowRelative = false);

}