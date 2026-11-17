# Executed by LONGMARCH_RESEARCH_SELFTEST in the simulator's actual worker.
# No construction data: test bridge contracts and standard RLE behavior.
class CellList(list):
    def __getitem__(self, index):
        raise RuntimeError("Bridge must not invoke a list subclass's custom indexing")


cells = CellList([CellList([0, 0]), CellList([1, 0]),
                  CellList([0, 1]), CellList([1, 1])])
expected = {(0, 0), (1, 0), (0, 1), (1, 1)}
assert set(map(tuple, life_evolve(cells, 32))) == expected
encoded = life_encode_rle(cells)
assert set(map(tuple, life_parse_rle(encoded['rle']))) == expected
assert life_parse_rle('o10$2bo!') == [[0, 0], [2, 10]]
large_rle = 'x = 50, y = 6000, rule = B3/S23\n' + '31o17b2o3$' * 2000 + '!'
for _ in range(3):
    large_cells = life_parse_rle(large_rle)
    assert len(large_cells) == 66000 and large_cells[-1] == [49, 5997]
print('runtime checkpoint: subclass conversion and codecs passed', flush=True)
result = {'subclass_bridge': True, 'codecs': True}
