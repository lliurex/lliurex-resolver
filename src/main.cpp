/*
    lliurex resolver
    
    Copyright (C) 2019  Enrique Medina Gremaldos <quiqueiii@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <apt-pkg/init.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/pkgsystem.h>

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <map>
#include <vector>

using namespace std;

enum class Option
{ None = 0, UseInput = 1, UseOutput = 2, UseBanned = 4,
    Verbose = 8, DumpProvide = 16, AddBootstrap = 32, ComputeBootstrap = 64 };

class Application
{
    public:

    map <string,string> bootstrap;
    map <string,string> depmap;
    map <string,vector <string> >prvmap;
    map <string,string> virtuals;
    vector <pkgCache::DepIterator> multiples;
    map <string,string> banmap;

    string filename;
    bool dump_provide;
    bool add_bootstrap;
    bool compute_bootstrap;

    pkgCache* cache;

    /*!
       Constructor
       application start point
     */
    Application (int argc, char *argv[])
    {

        vector <string> targets;
        vector <string> bad_targets;

        filename = "";
        dump_provide = false;
        add_bootstrap = false;
        compute_bootstrap = false;

        if (argc < 2) {
            print_usage();
            return;
        }

        Option option = Option::None;

        for (int n = 0; n < argc; n++) {
            string arg = argv[n];

            if (arg == "-i") {
                option = Option::UseInput;
                continue;
            }

            if (arg == "-o") {
                option = Option::UseOutput;
                continue;
            }

            if (arg == "-b") {
                option = Option::UseBanned;
                continue;
            }

            if (arg == "-p") {
                option = Option::DumpProvide;
                dump_provide = true;
                continue;
            }

            if (arg == "-d") {
                option = Option::AddBootstrap;
                add_bootstrap = true;
                continue;
            }
            
            if (arg == "-c") {
                option = Option::ComputeBootstrap;
                compute_bootstrap = true;
                continue;
            }
            
            switch (option) {
                case Option::UseOutput:
                    filename = arg;
                    break;

                case Option::UseInput:
                    targets.push_back (arg);
                    break;

                case Option::UseBanned:
                    banmap[arg] = "";
                    break;
            }
        }

        pkgInitConfig(*_config);
        pkgInitSystem(*_config, _system);

        pkgCacheFile cache_file;
        cache = cache_file.GetPkgCache();

        /* building bootstrap and provide map */
        cout << "* Building cache..." << endl;

        for (pkgCache::PkgIterator pkg = cache->PkgBegin(); !pkg.end(); pkg++) {
            
            if (is_virtual(pkg)) {
              continue;
            }

            //we just take first available version
            pkgCache::VerIterator ver = pkg.VersionList();

            if (ver->Priority == pkgCache::State::Required ||
              ver->Priority == pkgCache::State::Important) {
                
                bootstrap[pkg.Name()] = ver.VerStr();
            }

            for (pkgCache::PrvIterator prv = ver.ProvidesList(); !prv.end(); prv++) {

                pkgCache::PkgIterator owner = prv.OwnerPkg();

                string pname = prv.Name();
                string oname = pkg.Name();

                if (pname != oname) {
                    prvmap[pname].push_back(oname);
                }
            }
        }

        if (compute_bootstrap) {
            //ignore add bootsrap option
            add_bootstrap=false;
            
            for (std::pair<string,string> package : bootstrap) {
                targets.push_back(package.first);
            }
        }
        
        //main targets
        for (string target:targets) {
            pkgCache::PkgIterator pkg;
            string vname;

            try {
              pkg = find(target);
            }
            catch (runtime_error e) {
                
                //package does not exists
                bad_targets.push_back(target);
                continue;
            }

            if (is_virtual(pkg)) {
                try {
                    vname = resolve_provide(target);
                }
                catch (runtime_error e) {
                    
                    //no one provide this virtual package
                    bad_targets.push_back(target);
                    continue;
                }

                //using provided name
                pkg = find (vname);
            }

            depmap[pkg.Name ()] = "";
            cout << "[ 0]->" << pkg.Name() << endl;
            build (pkg.VersionList(), 1);
        }

        map <string,pkgCache::DepIterator> newdep;

        /* solving multiple choices */
        recompute_multiples:

        cout << "multiple choices:" << endl;

        for (pkgCache::DepIterator q:multiples) {
            pkgCache::DepIterator dep = q;

            cout << "* (";

            while (true) {
                string pkgname = dep.TargetPkg().Name();
                cout << pkgname;

                if ((dep->CompareOp & pkgCache::Dep::Or) !=
                    pkgCache::Dep::Or) {
                    break;
                }
                
                dep++;
                cout << " | ";
            }
            cout << ")" << endl;
        }

        for (pkgCache::DepIterator q:multiples) {
            pkgCache::DepIterator dep = q;

            bool found = false;

            while (true) {

                string pkgname = dep.TargetPkg().Name();

                if (depmap.find(pkgname) != depmap.end()) {
                    cout << "Using " << pkgname << " already included" << endl;
                    
                    found = true;
                    break;
                }

                if (bootstrap.find(pkgname) != bootstrap.end()) {
                    cout << "Using " << pkgname << " from bootstrap" << endl;
                    found = true;
                    break;
                }

                if ((dep->CompareOp & pkgCache::Dep::Or) !=
                    pkgCache::Dep::Or) {
                    break;
                }
                
                dep++;
            }

            if (!found) {

                /* 
                   no choice has be done, let's look for a 
                   not banned option    
                 */

                pkgCache::DepIterator t = q;

                while (true) {

                    string pkgname = t.TargetPkg().Name();

                    if (banmap.find(pkgname) == banmap.end()) {
                        if (newdep.find(pkgname) == newdep.end()) {
                            cout << "Adding " << pkgname << endl;
                            newdep[pkgname] = t;
                            break;
                        }
                        else {
                            //already queued for resolve
                            break;
                        }
                    }
                    else {
                        cout << "Avoiding " << pkgname << endl;
                    }

                    if ((t->CompareOp & pkgCache::Dep::Or) !=
                        pkgCache::Dep::Or) {
                        break;
                    }
                    
                    t++;
                }
            }
        }

        multiples.clear ();

        for (pair < string, pkgCache::DepIterator > q:newdep) {
            cout << "* RECOMPUTING: " << q.first << endl;

            pkgCache::PkgIterator pkg = q.second.TargetPkg();

            if (is_virtual(pkg)) {
                try {
                    string pkgname = pkg.Name();
                    string vname = resolve_provide(pkgname);
                    cout << pkgname << " is a virtual package, using " <<
                        vname << endl;

                    if (bootstrap.find(vname) == bootstrap.end()) {
                        if (depmap.find(vname) == depmap.end()) {
                            pkgCache::PkgIterator provider = find(vname);

                            depmap[vname] = "";
                            build (provider.VersionList(), 0);
                        }
                    }
                }
                catch (runtime_error e) {
                    cout << "Could not find a provide for: " << pkg.Name() << endl;
                    cout << pkg.Name() << " has been banned" << endl;
                    banmap[pkg.Name()] = "";
                    multiples.push_back(q.second);
                }
            }
            else {
                depmap[pkg.Name()] = "";
                build (pkg.VersionList(), 0);
            }
        }

        if (newdep.size() > 0) {
            newdep.clear();
            goto recompute_multiples;
        }

        //does it ever happen?
        cout << "Missing multiples:" << multiples.size() << endl;

        if (bad_targets.size() > 0) {
            cout << "Bad inputs:" << endl;

            for (string target:bad_targets) {
                cout << "* " << target << endl;
            }
        }

        //bootstrap count is included too
        int total = depmap.size();
        if (add_bootstrap) {
            total += bootstrap.size();
        }
        
        cout << "Total:" << total << endl;

        if (dump_provide) {
            if (prvmap.size () > 0) {
                cout << "Provide : Provided from" << endl;
                
                for (pair < string, vector < string > >p:prvmap) {
                    cout << "* " << p.first << " : ";
                    for (string s:p.second) {
                        cout << s << " ";
                    }
                    cout << endl;
                }
                cout << "Total provides: " << prvmap.size() << endl;
            }
        }
    }

    /*!
       Finds a package iterator from apt cache
       \param pkgname string matching package name
     */
    pkgCache::PkgIterator find(string pkgname)
    {
        pkgCache::PkgIterator pkg = cache->FindPkg(pkgname);

        if (pkg.end()) {
            throw runtime_error("package does not exists");
        }
        
        return pkg;
    }

    /*!
       Recursive dependency build
       \param ver Version start point
       \param depth used for debugging, use 0
     */
    void build(pkgCache::VerIterator ver, int depth)
    {
        bool last_or = false;

        for (pkgCache::DepIterator dep = ver.DependsList(); !dep.end(); dep++) {

            if (dep->Type == pkgCache::Dep::Depends ||
              dep->Type == pkgCache::Dep::Recommends ||
              dep->Type == pkgCache::Dep::PreDepends) {

                //deferred resolution of multiple choices
                if ((dep->CompareOp & pkgCache::Dep::Or) ==
                pkgCache::Dep::Or) {
                    if (!last_or) {
                        last_or = true;
                        multiples.push_back(dep);
                    }
                    continue;
                }
                else {
                    if (last_or) {
                        last_or = false;
                        continue;
                    }
                }

                pkgCache::PkgIterator pkg = dep.TargetPkg();
                string pkgname = pkg.Name();

                if (is_virtual(pkg)) {
                    try {
                        string vname = resolve_provide (pkgname);
                        cout << pkgname <<
                            " is a virtual package, using " << vname << endl;

                        if (bootstrap.find(vname) == bootstrap.end()) {
                            if (depmap.find(vname) == depmap.end()) {
                                pkgCache::PkgIterator provider = find(vname);

                                depmap[vname] = "";
                                cout << "[" << setw (2) << depth << "]";
                                for (int n = 0; n < depth; n++) {
                                    cout << "-";
                                }
                                cout << "->" << vname << endl;
                                build (provider.VersionList(), depth + 1);
                            }
                        }
                    }
                    catch (runtime_error e) {
                        banmap[pkgname] = "";
                    }
                }
                else {
                    if (depmap.find(pkgname) == depmap.end()) {
                        for (pkgCache::VerIterator ver =
                             pkg.VersionList(); !ver.end(); ver++) {
                            
                            if (dep.IsSatisfied(ver)) {
                                
                                /* check against bootstrap package list */
                                if (bootstrap.find(pkgname) ==
                                    bootstrap.end()) {
                                    depmap[pkgname] = ver.VerStr();
                                    cout << "[" << setw (2) << depth << "]";
                                
                                    for (int n = 0; n < depth; n++) {
                                        cout << "-";
                                    }
                                    cout << "->" << pkgname << endl;
                                    build (ver, depth + 1);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    /*!
       Resolves a provide
     */
    string resolve_provide(string prvname)
    {
        string ret = "";

        if (virtuals.find(prvname) == virtuals.end()) {

            if (prvmap.find(prvname) != prvmap.end()) {
                for (string s:prvmap[prvname]) {
                    if (depmap.find(s) != depmap.end()) {
                        ret = s;
                        break;
                    }
                }

                if (ret == "") {
                    ret = prvmap[prvname][0];
                }
            }

            virtuals[prvname] = ret;
        }
        else {
            ret = virtuals[prvname];
        }

        if (ret == "") {
            throw runtime_error ("Could not find provide " + prvname);
        }
        
        return ret;
    }

    /*!
       Checks whenever a package is virtual
     */
    bool is_virtual(pkgCache::PkgIterator pkg)
    {
        pkgCache::VerIterator ver = pkg.VersionList();

        return (ver.end());
    }

    /*!
       Dump result
     */
    void dump()
    {
        if (filename != "") {
            cout << "Output to: " << filename << endl;

            fstream f;
            f.open (filename, std::fstream::out);

            for (pair <string,string> q:depmap) {
                f << q.first << "\tinstall" << endl;
            }

            if (add_bootstrap) {
                for (pair <string,string> q:bootstrap) {
                    f << q.first << "\tinstall" << endl;
                }
            }

            f.close();
        }
    }

    void print_usage()
    {
        cout << "usage: " << endl;
        cout << "lliurex-resolver [OPTIONS]" << endl;
        cout << endl;

        cout << "Available options:" << endl;
        cout << "-i <targets>\tInput packages" << endl;
        cout << "-o <file>\tOutput file" << endl;
        cout << "-d adds bootstrap to output list" << endl;
        cout << "-c adds bootstrap to output list recomputing dependency tree. This will ignore -d" << endl;
        cout << "-b <targets>\tPackages to avoid" << endl;
        cout << "-p\tDumps provide list" << endl;
    }
};

int main (int argc, char *argv[])
{
    Application app(argc, argv);
    app.dump();

    return 0;
}
