"""Fail-closed regression checks for the fullscreen system acceptance gate."""
import copy
import json
import unittest

from quality_fullscreen_system import orientation_summary, paint_summary, preparation_summary, continuity_summary, CONTINUITY_ROWS


def output(prefix, records):
    return '\n'.join(prefix + ' ' + json.dumps(record) for record in records)


def orientation_records():
    rows = ('identity', 'rotate90', 'rotate180', 'rotate270', 'mirror', 'flip')
    metrics = [dict(row=row, entering=entering, completed=True, observed=True,
                    source_matches=True, rendered_matches=True, geometry_matches=True,
                    source_width=4096, source_height=3072, request_cpu_ms=3,
                    request_wall_ms=5) for row in rows for entering in (True, False)]
    reloads = [dict(row=row, observed=True, source_matches=True) for row in rows]
    return metrics, reloads


def orientation_output(metrics, reloads):
    return output('FULLSCREEN_COLD_ORIENTATION', metrics) + '\n' + output('FULLSCREEN_SOURCE_RELOAD', reloads)


class FullscreenSystemMetricsTests(unittest.TestCase):
    def test_continuity_requires_complete_valid_native_samples(self):
        records = [dict(row=row, samples=1, size_error=0, position_error=0,
                        bottom_error=0, bottom_pixel_errors=0) for row in CONTINUITY_ROWS]
        self.assertTrue(continuity_summary(output('FULLSCREEN_CONTINUITY', records))['passed'])
        for candidate in ([], records[:-1], records + records[:1]):
            self.assertFalse(continuity_summary(output('FULLSCREEN_CONTINUITY', candidate))['passed'])
        for field, value in (('samples', 0), ('samples', True), ('size_error', 584),
                             ('position_error', 567), ('bottom_pixel_errors', 2256),
                             ('bottom_error', float('nan')), ('size_error', None)):
            candidate = copy.deepcopy(records)
            candidate[0][field] = value
            self.assertFalse(continuity_summary(output('FULLSCREEN_CONTINUITY', candidate))['passed'])

    def test_hdr_and_sdr_observations_cannot_satisfy_each_others_gate(self):
        records = [dict(row=row, samples=1, size_error=0, position_error=0,
                        bottom_error=0, bottom_pixel_errors=0) for row in CONTINUITY_ROWS]
        hdr = output('HDR_FULLSCREEN_CONTINUITY', records)
        sdr = output('FULLSCREEN_CONTINUITY', records)
        self.assertFalse(continuity_summary(hdr)['passed'])
        self.assertFalse(continuity_summary(sdr, 'HDR_FULLSCREEN_CONTINUITY')['passed'])
        for prefix in ('FULLSCREEN_CONTINUITY', 'HDR_FULLSCREEN_CONTINUITY'):
            self.assertTrue(continuity_summary(sdr+'\n'+hdr, prefix)['passed'])
            self.assertFalse(continuity_summary(output(prefix, records[:-1]), prefix)['passed'])

    def test_complete_orientation_and_reload_matrix_passes(self):
        metrics, reloads = orientation_records()
        self.assertTrue(orientation_summary(orientation_output(metrics, reloads))['passed'])

    def test_missing_duplicate_or_failed_observations_cannot_pass(self):
        metrics, reloads = orientation_records()
        for candidate in ('', orientation_output(metrics[:-1], reloads),
                          orientation_output(metrics + metrics[:1], reloads),
                          orientation_output(metrics, reloads[:-1]),
                          orientation_output(metrics, reloads + reloads[:1])):
            with self.subTest(candidate=candidate[:70]):
                self.assertFalse(orientation_summary(candidate)['passed'])
        for field, value in (('completed', False), ('observed', False),
                             ('source_matches', False), ('rendered_matches', False),
                             ('geometry_matches', False), ('source_width', 3072),
                             ('source_height', 4096), ('entering', 1)):
            records = copy.deepcopy(metrics)
            records[0][field] = value
            with self.subTest(field=field):
                self.assertFalse(orientation_summary(orientation_output(records, reloads))['passed'])
        reloaded = copy.deepcopy(reloads)
        reloaded[0]['source_matches'] = False
        self.assertFalse(orientation_summary(orientation_output(metrics, reloaded))['passed'])

    def test_missing_or_invalid_timings_cannot_pass(self):
        metrics, reloads = orientation_records()
        for field in ('request_cpu_ms', 'request_wall_ms'):
            for value in (None, True, -1, float('nan'), float('inf')):
                records = copy.deepcopy(metrics)
                records[0][field] = value
                with self.subTest(field=field, value=value):
                    self.assertFalse(orientation_summary(orientation_output(records, reloads))['passed'])
            records = copy.deepcopy(metrics)
            records[0].pop(field)
            self.assertFalse(orientation_summary(orientation_output(records, reloads))['passed'])

    def test_paint_cpu_budget_does_not_hide_duplicates(self):
        metrics = [dict(hidden=h, vector=v, **{'pass': i}, paints=1,
                        elapsed_ms=150, cpu_ms=41)
                   for h in (False, True) for v in (False, True) for i in range(3)]
        self.assertTrue(paint_summary(output('FULLSCREEN_PAINT_BUDGET', metrics))['passed'])
        for field, value in (('paints', 2), ('cpu_ms', 76), ('cpu_ms', None),
                             ('cpu_ms', float('nan')), ('elapsed_ms', -1)):
            records = copy.deepcopy(metrics)
            records[0][field] = value
            with self.subTest(field=field, value=value):
                self.assertFalse(paint_summary(output('FULLSCREEN_PAINT_BUDGET', records))['passed'])

    def test_preparation_cpu_budget_still_requires_one_endpoint_paint(self):
        metrics = [dict(row=f'{title}-{kind}', entering=entering, completed=True,
                        source_paints=1, source_cost_ms=150, source_cpu_ms=41,
                        endpoint_paints=1, endpoint_cost_ms=150, endpoint_cpu_ms=41,
                        samples=[{'endpoint': True}])
                   for title in ('visible', 'hidden') for kind in ('raster', 'vector')
                   for entering in (True, False)]
        self.assertTrue(preparation_summary(output('FULLSCREEN_PREPARATION', metrics))['passed'])
        for field, value in (('endpoint_paints', 0), ('endpoint_paints', 2),
                             ('source_cpu_ms', 76), ('endpoint_cpu_ms', 76),
                             ('endpoint_cpu_ms', None), ('source_cpu_ms', float('nan'))):
            records = copy.deepcopy(metrics)
            records[0][field] = value
            with self.subTest(field=field, value=value):
                self.assertFalse(preparation_summary(output('FULLSCREEN_PREPARATION', records))['passed'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
