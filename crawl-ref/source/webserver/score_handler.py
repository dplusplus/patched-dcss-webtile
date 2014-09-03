# -*- coding: utf-8 -*-
import tornado.web

import itertools
import os.path
import re

from morgue_handler import RequestHandler
from config import max_score_list_length

import sys
import locale


class ScoreTopNHandler(RequestHandler):
    def get(self, num):
        num = min(int(num), max_score_list_length)

        class Score():
            def __init__(self, dic):
                self.dic = dic

            def __getattr__(self, name):
                return self.dic.get(name, '')

        def parse_scores(scores_path):
            def parse_scoreline(line):
                dic = dict(line=str(line.strip().split(':')))
                for token in line.strip().split(':'):
                    if token.count('=') == 1:
                        attr, value = token.split('=')
                        dic[attr] = value

                locale.setlocale(locale.LC_NUMERIC, 'ja_JP.utf8')
                dic['sc'] = locale.format("%d", int(dic['sc']), grouping=True)
                dic['turn'] = locale.format("%d", int(dic['turn']), grouping=True)

                if 'end' in dic:
                    pattern = re.compile('(?P<year>\d{4})(?P<month>\d{2})(?P<day>\d{2})(?P<hhmmss>\d{6})S')
                    m = pattern.match(dic['end'])
                    if m:
                        dic['dumpURL'] = '../../morgue/{0}/morgue-{0}-{1}{2}{3}-{4}.txt'.format(
                            dic['name'],
                            m.group('year'),
                            '{0:0>2}'.format(int(m.group('month'))+1),
                            m.group('day'),
                            m.group('hhmmss')
                        )

                return Score(dic)

            with open(scores_path) as f:
                return [parse_scoreline(line) for line in itertools.islice(f, num)]

        self.render('top-N.html', scores=parse_scores('./saves/scores'), num=num)
