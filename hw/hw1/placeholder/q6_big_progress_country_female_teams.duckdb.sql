with participant_country as(
    select code, country_code from athletes
    union
    select code, country_code from teams
),
gold_count as(
    select country_code, count(1) as golds
    from 
        (medals left join medal_info on medals.medal_code = medal_info.code) as m
        left join
        participant_country as p
        on m.winner_code = p.code
    where name like 'Gold Medal'
    group by country_code
),
top5_impovement as(
    select gold_count.country_code as country_code, golds - gold_medal as increase
    from gold_count, tokyo_medals
    where gold_count.country_code = tokyo_medals.country_code
    order by increase desc
    limit 5
)

select 
    any_value(a.country_code) as country_code, 
    any_value(increase) as increased_gold_medal_number,
    a.code as team_code, 
from 
    (top5_impovement as t left join teams on t.country_code = teams.country_code) as a
    left join athletes
    on a.athletes_code = athletes.code
group by team_code 
having sum(gender != 1) = 0
order by 
    increased_gold_medal_number desc,
    country_code,
    team_code