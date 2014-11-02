# -*- coding: utf-8 -*-
import os.path

import tornado.web

from morgue_handler import RequestHandler
import mapping


class MetaFileIndexHandler(RequestHandler):
    metafiles = [
        'logfile', 'logfile-sprint', 'logfile-zotdef',
        'milestones', 'milestones-sprint',
        'milestones-zotdef', 'milestones-tutorial'
    ]
    metafiles.extend(mapping.scores.iterkeys())
    branches = list(mapping.version_path.iterkeys())

    def get(self, branch, _):
        if branch:
            self.renderMetaFileIndex(branch.rstrip('/'))
        else:
            self.renderBranchIndex()

    def renderMetaFileIndex(self, branch):
        params = {
            'files': self.metafiles,
            'branch': branch,
            'pwd' : 'meta',
        }
        self.render('metafile_index.html', **params)

    def renderBranchIndex(self):
        self.render('branch_index.html', branches=self.branches, pwd='meta')


class MetaFileHandler(RequestHandler):
    def get(self, branch, path):
        branches = MetaFileIndexHandler.branches
        metafiles = MetaFileIndexHandler.metafiles

        if (branch in branches) and (path in metafiles):
            branch = ('dcss-' + branch) if branch != 'trunk' else 'crawl-ref'

            self.set_header('Content-Type', 'text/plain; charset="utf-8"')
            path = '../../{0}/source/saves/{1}'.format(branch, path)

            if os.path.exists(path):
                self.write(open(path).read())
            else:
                raise tornado.web.HTTPError(404)
        else:
            raise tornado.web.HTTPError(403)


class RCFileIndexHandler(RequestHandler):
    branches = list(mapping.version_path.iterkeys())

    def get(self, branch, _):
        if branch:
            self.renderRCFileIndex(branch.rstrip('/'))
        else:
            self.renderBranchIndex()

    def renderRCFileIndex(self, branch):
        path = '../../{0}/source/rcs/'.format(
            ('dcss-' + branch) if branch != 'trunk' else 'crawl-ref'
            )
        params = {
            'files': sorted(f for f in os.listdir(path) if f.endswith('.rc')),
            'branch': branch,
            'pwd': 'rcfiles',
        }
        self.render('metafile_index.html', **params)

    def renderBranchIndex(self):
        self.render('branch_index.html', branches=self.branches, pwd='rcfiles')


class RCFileHandler(RequestHandler):
    def get(self, branch, path):
        branches = MetaFileIndexHandler.branches

        if branch in branches:
            branch = ('dcss-' + branch) if branch != 'trunk' else 'crawl-ref'

            self.set_header('Content-Type', 'text/plain; charset="utf-8"')
            path = '../../{0}/source/rcs/{1}'.format(branch, path)

            if os.path.exists(path):
                self.write(open(path).read())
            else:
                raise tornado.web.HTTPError(404)
        else:
            raise tornado.web.HTTPError(403)
